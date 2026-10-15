#include "machine.h"
#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* A service session attaches the bench loopback cable and the general DMA
   controller the firmware's factory path needs. It is off for every ordinary
   boot so the handoff state is reproduced unchanged. */
int sgh_service_mode = 0;
static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0; return s;
}
static int number(char *s, uint32_t *v)
{
    char *end;
    unsigned long long n;
    s = trim(s);
    if (*s == '-') return 0;
    n = strtoull(s, &end, 0);
    if (s == end || *trim(end) || n > UINT32_MAX) return 0;
    *v = (uint32_t)n; return 1;
}
static uint32_t identity_read(void *ctx, uint32_t offset, unsigned width)
{
    Identity16 *id = ctx;
    if (width == 1) return (id->value >> (8 * offset)) & 255u;
    return id->value;
}
static void identity_write(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Identity16 *id = ctx;
    bus_fault_access(id->bus, 1, id->address + offset, width, value, "read-only identity fixture");
}
static uint32_t register_read(void *ctx, uint32_t offset, unsigned width)
{
    Register32 *r = ctx;
    uint32_t mask = width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1;
    return (r->value >> (offset * 8)) & mask;
}
static void register_write(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Register32 *r = ctx;
    if(r->irom_vector){bus_fault_access(r->bus,1,r->address+offset,width,value,"read-only internal-ROM vector");return;}
    uint32_t mask = width == 4 ? UINT32_MAX : ((1u << (width * 8)) - 1) << (offset * 8);
    r->value = (r->value & ~mask) | ((value << (offset * 8)) & mask);
}
static int image_path(char *out, size_t capacity, const char *config, const char *image)
{
    const char *slash, *back;
    size_t prefix = 0, n = strlen(image);
    if (image[0] != '/' && image[0] != '\\' && !(n >= 2 && image[1] == ':')) {
        slash = strrchr(config, '/'); back = strrchr(config, '\\');
        if (!slash || (back && back > slash)) slash = back;
        if (slash) prefix = (size_t)(slash - config) + 1;
    }
    if (prefix + n >= capacity) return 0;
    memcpy(out, config, prefix); memcpy(out + prefix, image, n + 1); return 1;
}
static void clock_rate(PhoneClock *c)
{
    unsigned i;uint32_t arm=c->reference_hz,dsp=c->reference_hz;
    c->unknown=0;
    if(c->registers[2]&8) {
        for(i=0;i<c->count;++i)if(c->registers[1]==c->encoding[i])break;
        if(i==c->count)c->unknown=1;
        else {if((c->registers[2]&3)==3)arm=c->arm_rates[i];if(c->registers[2]&4)dsp=c->dsp_rates[i];}
    }
    if(arm!=c->arm_hz)c->fraction=0; /* Less than one ns lost at a source switch. */
    c->arm_hz=arm;c->dsp_hz=dsp;
}
static uint32_t clock_read(void *context,uint32_t offset,unsigned width)
{
    PhoneClock *c=context;uint32_t value=0;unsigned i;
    if(offset+width>6){bus_fault_access(c->bus,0,c->base+offset,width,0,"cross-clock register access");return 0;}
    for(i=0;i<width;++i)value|=((c->registers[(offset+i)/2]>>(((offset+i)&1)*8))&255u)<<(i*8);
    return value;
}
static void clock_write(void *context,uint32_t offset,unsigned width,uint32_t value)
{
    PhoneClock *c=context;unsigned i;
    if(offset+width>6){bus_fault_access(c->bus,1,c->base+offset,width,value,"cross-clock register access");return;}
    for(i=0;i<width;++i){unsigned reg=(offset+i)/2,shift=((offset+i)&1)*8;
        c->registers[reg]=(uint16_t)((c->registers[reg]&~(255u<<shift))|(((value>>(i*8))&255u)<<shift));}
    clock_rate(c);
}
static void clock_advance(PhoneClock *c,uint64_t cycles)
{
    uint64_t scaled,delta=cycles-c->clock_cycles;
    c->clock_cycles=cycles;
    /* An instruction is a handful of cycles. delta*1e9 = q*hz + r exactly, so
       floor((delta*1e9 + fraction)/hz) = q + (r + fraction >= hz), with the
       identical remainder. Same result as the division, none of its cost
       (64-bit division is a library call in an x86 build). */
    if(delta<33&&c->arm_hz&&c->fraction<c->arm_hz) {
        uint32_t fraction,previous_fraction;
        if(c->table_hz!=c->arm_hz) {
            unsigned i;
            for(i=0;i<33;++i){uint64_t s=(uint64_t)i*1000000000u;c->step_ns[i]=s/c->arm_hz;c->step_rem[i]=(uint32_t)(s%c->arm_hz);}
            c->table_hz=c->arm_hz;
        }
        previous_fraction=(uint32_t)c->fraction;fraction=previous_fraction+c->step_rem[delta];
        c->nanoseconds+=c->step_ns[delta];
        /* Both operands are below hz. Carry-aware subtraction is exact even
           above 2 GHz, without a 64-bit addition/comparison on x86. */
        if(fraction<previous_fraction||fraction>=c->arm_hz){fraction-=c->arm_hz;++c->nanoseconds;}
        c->fraction=fraction;return;
    }
    scaled=delta*1000000000+c->fraction;
    c->nanoseconds+=scaled/c->arm_hz;c->fraction=scaled%c->arm_hz;
}
void machine_destroy(Machine *m)
{
    if (m->audio.ops) m->audio.ops->stop(&m->audio);
    phone_net_close(&m->network);
    ma3_destroy(&m->ma3);
    arm7_cache_destroy(m->decode_cache);phone_io_close(&m->phone_io); bus_destroy(&m->bus); free(m->pixels); free(m->unplaced_rom); memset(m, 0, sizeof(*m));
}
int machine_load(Machine *m, const char *path, const char *rom_override, FILE *log)
{
    return machine_load_pair(m, path, rom_override, NULL, log);
}
int machine_load_pair(Machine *m, const char *path, const char *rom_override,
    const char *second_override, FILE *log)
{
    FILE *f;
    char line[2048];
    unsigned line_no = 0;
    int rom = 0, reset_set = 0, valid = 1, second_used = 0;
    machine_destroy(m);cellular_init(&m->cellular);
    m->bus.log = log; m->audio = audio_silent(); m->width = 128; m->height = 128;
    if (strlen(path) >= sizeof(m->config_path)) { strcpy(m->error, "Model path is too long"); return 0; }
    strcpy(m->config_path, path);
    f = fopen(path, "r");
    if (!f) { snprintf(m->error, sizeof(m->error), "Cannot open model config: %.180s", path); return 0; }
    while (fgets(line, sizeof(line), f)) {
        char *key = trim(line), *value, *comma;
        uint32_t base, size;
        uint8_t *data;
        size_t n;
        ++line_no;
        if (!*key || *key == '#') continue;
        value = strchr(key, '='); if (!value) { valid = 0; break; }
        *value++ = 0; key = trim(key); value = trim(value);
        if(!strcmp(key,"phone_io")||!strcmp(key,"phone_tones")){
            unsigned count=0,expected=!strcmp(key,"phone_tones")?3u:7u;char *part=value;uint32_t fields[7];
            while(part&&count<expected){char *next=strchr(part,',');if(next)*next++=0;
                if(!number(trim(part),&fields[count++])){valid=0;break;}part=next;}
            if(!valid||count!=expected||part){valid=0;break;}
            m->phone_io.tone_entry=fields[0];m->phone_io.tone_stop=fields[1];
            m->phone_io.tone_table=fields[2];
            if(expected==7){m->phone_io.gpio_entry=fields[3];
                if(!m->phone_io.hardware_led){m->phone_io.led_pins[0]=fields[4];m->phone_io.led_pins[1]=fields[5];m->phone_io.led_pins[2]=fields[6];}}
            m->phone_io.configured=1;continue;
        }
        if (!strcmp(key, "name")) { if (strlen(value) >= sizeof(m->name)) { valid = 0; break; } strcpy(m->name, value); continue; }
        if (!strcmp(key, "reset")) { if (!number(value, &m->reset)) { valid = 0; break; } reset_set = 1; continue; }
        if (!strcmp(key, "audio")) { if (strcmp(value, "silent")) { valid = 0; break; } continue; }
        if(!strcmp(key,"cellular_select")){
            if(m->cellular.radio_select||!number(value,&base)||!base||(base&1)){valid=0;break;}
            m->cellular.radio_select=base;continue;
        }
        if (!strcmp(key, "sim_present")) {
            uint32_t present;
            if (!m->sim.attached || !number(value,&present) || present>1) { valid=0; break; }
            m->sim.present=(int)present; continue;
        }
        if (!strcmp(key,"sim_card")) {
            char card_path[1024];
            if (!m->sim.attached || !image_path(card_path,sizeof(card_path),path,value) || !sim_card_load(&m->sim.card,card_path)) { valid=0; break; }
            m->sim.card.response_size=0;continue;
        }
        if (!strcmp(key, "diagnostic")) { if (strcmp(value, "continue")) { valid = 0; break; } m->bringup = 1; continue; }
        if (!strcmp(key, "intc")) {
            uint32_t address;
            if (m->intc.attached || !number(value, &address) || !intc_attach(&m->intc, &m->bus, address)) { valid = 0; break; }
            continue;
        }
        if(!strcmp(key,"intc_aux")){
            uint32_t address;
            if(!m->intc.attached||m->intc_aux.attached||!number(value,&address)||
                !intc_attach(&m->intc_aux,&m->bus,address)){valid=0;break;}
            m->intc.secondary=&m->intc_aux;continue;
        }
        if (!strcmp(key, "unplaced_rom")) {
            if (m->unplaced_rom || !image_path(m->unplaced_path, sizeof(m->unplaced_path),
                second_override ? "" : path, second_override ? second_override : value) ||
                !file_read(m->unplaced_path, &m->unplaced_rom, &m->unplaced_size)) { valid = 0; break; }
            second_used = 1;
            continue;
        }
        comma = strchr(value, ','); if (!comma) { valid = 0; break; }
        *comma++ = 0; if (!number(value, &base)) { valid = 0; break; }
        if (!strcmp(key, "rom")) {
            RomImage *image;
            char *span = strchr(comma, ',');
            const char *override = m->rom_count == 0 ? rom_override : m->rom_count == 1 ? second_override : NULL;
            const char *filename;
            uint32_t mapped_size = 0;
            if (m->rom_count == 4) { valid = 0; break; }
            if (span) { *span++ = 0; if (!number(span, &mapped_size)) { valid = 0; break; } }
            image = &m->roms[m->rom_count];
            if (!image_path(image->path, sizeof(image->path), override ? "" : path, override ? override : trim(comma))) { valid = 0; break; }
            filename = image->path;
            if (!file_read(filename, &data, &n)) { valid = 0; break; }
            image->base = base; image->size = n;
            if (!mapped_size) mapped_size = (uint32_t)n;
            if (n > mapped_size || mapped_size > 32u * 1024 * 1024) { free(data); valid = 0; break; }
            if (mapped_size > n) {
                uint8_t *padded = realloc(data, mapped_size);
                if (!padded) { free(data); valid = 0; break; }
                data = padded; memset(data + n, 255, mapped_size - n);
            }
            if (!bus_map(&m->bus, base, mapped_size, data, 1, 1)) { free(data); valid = 0; break; }
            if (m->rom_count == 1) second_used = 1;
            ++m->rom_count;
            rom = 1;
        } else if (!strcmp(key, "irom_service")) {
            if (!irom_add(&m->irom, base, trim(comma))) { valid = 0; break; }
        } else if (!strcmp(key, "irom_version")) {
            char *next = strchr(comma, ',');
            uint32_t third;
            if (!next || m->irom.version_configured) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || size > 65535 || !number(next, &third) || (third & 1) ||
                !irom_add(&m->irom, base, "version")) { valid = 0; break; }
            m->irom.version = (uint16_t)size; m->irom.version_return = third; m->irom.version_configured = 1;
        } else if (!strcmp(key, "keypad") || !strcmp(key, "key")) {
            char *next = strchr(comma, ','); uint32_t column;
            if (!next) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || !number(next, &column)) { valid = 0; break; }
            if (!strcmp(key, "keypad")) {
                if (!keypad_attach(&m->keypad, &m->bus, base, size, column)) { valid = 0; break; }
            } else if (base >= SGH_MAX_KEYS || !keypad_map(&m->keypad, base, size, column)) { valid = 0; break; }
        } else if (!strcmp(key,"ma3")) {
            char *next=strchr(comma,',');uint32_t quantum;
            if(!next){valid=0;break;}*next++=0;
            if(!number(comma,&size)||!number(next,&quantum)||!ma3_attach(&m->ma3,&m->bus,&m->intc,base,size,quantum)){valid=0;break;}
        } else if (!strcmp(key, "sim")) {
            char *next=strchr(comma,','); uint32_t dma;
            if (!next) { valid=0; break; } *next++=0;
            if (!number(comma,&size) || !number(next,&dma) || !sim_attach(&m->sim,&m->bus,&m->intc,base,size,dma)) { valid=0; break; }
        } else if (!strcmp(key, "nv_provision")) {
            char *next = strchr(comma, ','); uint32_t marker;
            if (!next || m->nv_trigger || !base || (base & 1)) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || !size || (size & 1) || !number(next, &marker) || !marker || (marker & 1)) { valid = 0; break; }
            m->nv_trigger = base; m->nv_format = size; m->nv_marker = marker;
        } else if (!strcmp(key, "import")) {
            char *second = strchr(comma, ','), *third; uint32_t alloc, search, write;
            if (!second || m->import_trigger || !base || (base & 1)) { valid = 0; break; } *second++ = 0;
            third = strchr(second, ','); if (!third) { valid = 0; break; } *third++ = 0;
            if (!number(comma, &alloc) || !alloc || (alloc & 1) || !number(second, &search) || !search || (search & 1) ||
                !number(third, &write) || !write || (write & 1)) { valid = 0; break; }
            m->import_trigger = base; m->import_alloc = alloc; m->import_search = search; m->import_write = write;
        } else if (!strcmp(key, "java_import")) {
            uint32_t fields[5];unsigned j;char *part=comma;
            if(m->java_ready||!base||(base&1)){valid=0;break;}
            for(j=0;j<5;++j){char *next=strchr(part,',');
                if((j<4&&!next)||(j==4&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid||(fields[0]&3)||(fields[1]&1)||(fields[2]&1)){valid=0;break;}
            m->java_ready=base;m->java_globals=fields[0];m->java_set_jar=fields[1];
            m->java_send_ui=fields[2];m->java_message=fields[3];m->java_min_instructions=fields[4];
        } else if (!strcmp(key,"cellular")) {
            uint32_t fields[8];unsigned j;char *part=comma;
            if(m->cellular.ready||!base||(base&1)){valid=0;break;}
            for(j=0;j<8;++j){char *next=strchr(part,',');
                if((j<7&&!next)||(j==7&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid){break;}for(j=0;j<7;++j)if(fields[j]&1)valid=0;
            if(!valid||(fields[7]&3)){valid=0;break;}
            m->cellular.ready=base;m->cellular.alloc=fields[0];m->cellular.release=fields[1];
            m->cellular.send_ui=fields[2];m->cellular.dial=fields[3];m->cellular.accept=fields[4];
            m->cellular.disconnect=fields[5];m->cellular.name_lookup=fields[6];m->cellular.services=fields[7];
        } else if (!strcmp(key,"cellular_alert")) {
            if(!base||(base&1)||m->cellular.alert_request||!number(comma,&size)||size){valid=0;break;}m->cellular.alert_request=base;
        } else if (!strcmp(key,"cellular_poll")) {
            if(!base||(base&1)||m->cellular.poll||!number(comma,&size)||size){valid=0;break;}m->cellular.poll=base;
        } else if (!strcmp(key,"java_net")) {
            unsigned j;char *part=comma;
            if(m->network.java.entry[0]||!base||(base&1)){valid=0;break;}
            m->network.java.entry[0]=base;
            for(j=1;j<JN_COUNT;++j){char *next=strchr(part,',');
                if((j<JN_COUNT-1&&!next)||(j==JN_COUNT-1&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&m->network.java.entry[j])||!m->network.java.entry[j]||(m->network.java.entry[j]&1)){valid=0;break;}
                part=next;
            }
            if(!valid)break;
            m->network.java.first=UINT32_MAX;
            for(j=0;j<JN_COUNT;++j)if(j!=JN_NOTIFY){uint32_t pc=m->network.java.entry[j];
                if(pc<m->network.java.first)m->network.java.first=pc;
                if(pc>m->network.java.last)m->network.java.last=pc;}
        } else if (!strcmp(key,"wap")) {
            uint32_t fields[7];unsigned j;char *part=comma;
            if(m->network.txn_start||!base||(base&1)){valid=0;break;}
            for(j=0;j<7;++j){char *next=strchr(part,',');
                if((j<6&&!next)||(j==6&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid)break;for(j=0;j<5;++j)if(fields[j]&1)valid=0;
            if(!valid||(fields[5]&3)||(fields[6]&3)){valid=0;break;}
            m->network.txn_start=base;m->network.send_pdu=fields[0];m->network.recv_pdu=fields[1];
            m->network.bearer_state=fields[2];m->network.post_event=fields[3];m->network.data_arrived=fields[4];
            m->network.wap_base=fields[5];m->network.driver_base=fields[6];
        } else if (!strcmp(key,"cellular_sms")) {
            uint32_t fields[3];unsigned j;char *part=comma;
            if(m->cellular.sms_send||!base||(base&1)){valid=0;break;}
            for(j=0;j<3;++j){char *next=strchr(part,',');
                if((j<2&&!next)||(j==2&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid||(fields[0]&1)||(fields[1]&1)||(fields[2]&3)){valid=0;break;}
            m->cellular.sms_send=base;m->cellular.sms_reply=fields[0];m->cellular.send_message=fields[1];m->cellular.sr_task=fields[2];
        } else if (!strcmp(key,"java_buffers")) {
            uint32_t fields[4];unsigned j;char *part=comma;
            if(m->java_buffer_get||!base||(base&1)){valid=0;break;}
            for(j=0;j<4;++j){char *next=strchr(part,',');
                if((j<3&&!next)||(j==3&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid||(fields[0]&1)||(fields[1]&3)||(fields[2]&3)||
                fields[3]>0x100000u||(fields[3]&3)||
                (uint64_t)fields[2]+3u*fields[3]>UINT64_C(0x100000000)){valid=0;break;}
            m->java_buffer_get=base;m->java_buffer_set=fields[0];m->java_buffer_table=fields[1];
            m->java_buffer_base=fields[2];m->java_buffer_size=fields[3];
        } else if (!strcmp(key, "nvram")) {
            if (m->nvram_size || !number(comma, &size) || !size || (base & 1) || (size & 1)) { valid = 0; break; }
            m->nvram_base = base; m->nvram_size = size;
        } else if(!strcmp(key,"userfs")) {
            char *next=strchr(comma,','),*last;uint32_t capacity,stride;
            if(!next||m->fs_header){valid=0;break;}*next++=0;last=strchr(next,',');
            if(!last){valid=0;break;}*last++=0;
            if(!base||!number(comma,&size)||!number(next,&capacity)||!number(last,&stride)||!capacity||capacity>100||stride!=28){valid=0;break;}
            m->fs_header=base;m->fs_table=size;m->fs_capacity=capacity;m->fs_stride=stride;
        } else if (!strcmp(key,"lp_timer")) {
            uint32_t fields[4];unsigned j;char *part=comma;
            for(j=0;j<4;++j){char *next=strchr(part,',');if((j<3&&!next)||(j==3&&next)){valid=0;break;}
                if(next)*next++=0;if(!number(trim(part),&fields[j])){valid=0;break;}part=next;}
            if(!valid||!lp_timer_attach(&m->lp_timer,&m->bus,&m->intc,&m->clock.nanoseconds,
                base,fields[0],fields[1],fields[2],fields[3])){valid=0;break;}
        } else if (!strcmp(key,"dsp_mailbox")) {
            if(!number(comma,&size)||!dsp_mailbox_attach(&m->dsp_mailbox,&m->bus,base,size)){valid=0;break;}
        } else if (!strcmp(key,"battery_adc")) {
            char *next=strchr(comma,',');uint32_t temperature;
            if(m->battery.enabled||!m->dsp_mailbox.attached||!next){valid=0;break;}*next++=0;
            if(base>255||!number(comma,&size)||size>255||base>size||!number(next,&temperature)||temperature>255){valid=0;break;}
            m->battery.adc=&m->dsp_mailbox;m->battery.adc_empty=base;m->battery.adc_full=size;
            m->battery.adc_temperature=temperature;m->battery.enabled=1;
            battery_set(&m->battery,100,0);
        } else if (!strcmp(key,"gpio_port")) {
            if(m->gpio_count==5||!number(comma,&size)||!gpio_port_attach(&m->gpio_ports[m->gpio_count],&m->bus,base,size)){valid=0;break;}
            ++m->gpio_count;
        } else if (!strcmp(key,"gpio_led")) {
            uint32_t pins[3];unsigned j;char *part=comma;GpioPort *port=NULL;
            for(j=0;j<m->gpio_count;++j)if(m->gpio_ports[j].base==base)port=&m->gpio_ports[j];
            if(!port||m->phone_io.hardware_led){valid=0;break;}
            for(j=0;j<3;++j){char *next=strchr(part,',');if((j<2&&!next)||(j==2&&next)){valid=0;break;}
                if(next)*next++=0;if(!number(part,&pins[j])||pins[j]>=8){valid=0;break;}part=next;}
            if(!valid||pins[0]==pins[1]||pins[0]==pins[2]||pins[1]==pins[2]){valid=0;break;}
            for(j=0;j<3;++j)m->phone_io.led_pins[j]=pins[j];
            m->phone_io.hardware_led=1;port->output_changed=phone_io_gpio_write;port->output_context=&m->phone_io;
            phone_io_gpio_write(&m->phone_io,255,port->output,0);
        } else if (!strcmp(key,"charger_complete")) {
            uint32_t fields[5];unsigned j;char *part=comma;GpioPort *port=NULL;
            for(j=0;j<m->gpio_count;++j)if(m->gpio_ports[j].base==base)port=&m->gpio_ports[j];
            for(j=0;j<5;++j){char *next=strchr(part,',');if((j<4&&!next)||(j==4&&next)){valid=0;break;}
                if(next)*next++=0;if(!number(part,&fields[j])){valid=0;break;}part=next;}
            if(!valid||!port||port->output_changed||m->battery.complete_port||!m->clock.attached||
                fields[0]>=8||fields[1]>=8||fields[0]==fields[1]||fields[2]>1||fields[3]>=32||
                !(fields[2]?m->intc_aux.attached:m->intc.attached)){valid=0;break;}
            m->battery.complete_port=port;m->battery.complete_intc=fields[2]?&m->intc_aux:&m->intc;
            m->battery.complete_bit=fields[0];m->battery.enable_bit=fields[1];m->battery.complete_irq=fields[3];
            m->battery.complete_delay_ms=fields[4];m->battery.complete_due=UINT64_MAX;
            m->battery.nanoseconds=&m->clock.nanoseconds;
            gpio_port_input(port,fields[0],0);port->output_changed=battery_charger_output;port->output_context=&m->battery;
            battery_set(&m->battery,m->battery.percent,(int)m->battery.charging);
        } else if (!strcmp(key,"charger_input")) {
            if(!m->gpio_count||!m->intc.attached||m->battery.charger_port||base>=8||
                !number(comma,&size)||size>=32){valid=0;break;}
            m->battery.charger_port=&m->gpio_ports[m->gpio_count-1];m->battery.charger_intc=&m->intc;
            m->battery.charger_bit=base;m->battery.charger_irq=size;
            gpio_port_input(m->battery.charger_port,base,!m->battery.charging);
        } else if (!strcmp(key,"battery_report")) {
            char *next=strchr(comma,',');uint32_t report;
            if(m->battery.enabled||!base||(base&1)||!next){valid=0;break;}*next++=0;
            if(!number(comma,&size)||!size||(size&1)||!number(next,&report)||!report||(report&1)){valid=0;break;}
            m->battery.ready=base;m->battery.alloc=size;m->battery.report=report;
            m->battery.enabled=1;m->battery.percent=100;m->battery.level_pending=1;
        } else if (!strcmp(key,"battery_guest")) {
            uint32_t fields[5];unsigned j;char *part=comma;
            if(m->battery.enabled||!base||(base&1)){valid=0;break;}
            for(j=0;j<5;++j){char *next=strchr(part,',');
                if((j<4&&!next)||(j==4&&next)){valid=0;break;}if(next)*next++=0;
                if(!number(part,&fields[j])||!fields[j]){valid=0;break;}part=next;}
            if(!valid||(fields[0]&1)||(fields[1]&1)||(fields[2]&1)||(fields[3]&1)){valid=0;break;}
            m->battery.ready=base;m->battery.alloc=fields[0];m->battery.report=fields[1];
            m->battery.detect=fields[2];m->battery.gpio=fields[3];m->battery.pin=fields[4];
            m->battery.enabled=1;m->battery.percent=100;m->battery.level_pending=1;
        } else if (!strcmp(key,"rtc")) {
            if(!number(comma,&size)||!rtc_attach(&m->rtc,&m->bus,base,size,&m->clock.nanoseconds)){valid=0;break;}
        } else if (!strcmp(key,"clock")) {
            if(m->clock.attached||base&1||!number(comma,&size)||size<1000000||size>100000000){valid=0;break;}
            m->clock.bus=&m->bus;m->clock.base=base;m->clock.reference_hz=m->clock.arm_hz=m->clock.dsp_hz=size;
            if(!bus_device(&m->bus,base,6,clock_read,clock_write,&m->clock)){valid=0;break;}m->clock.attached=1;
        } else if(!strcmp(key,"clock_mode")) {
            char *next=strchr(comma,',');uint32_t dsp;unsigned index=m->clock.count;
            if(!m->clock.attached||!next||index==8||base>65535){valid=0;break;}*next++=0;
            if(!number(comma,&size)||!number(next,&dsp)||size<1000000||size>100000000||dsp<1000000||dsp>100000000){valid=0;break;}
            m->clock.encoding[index]=(uint16_t)base;m->clock.arm_rates[index]=size;m->clock.dsp_rates[index]=dsp;++m->clock.count;
        } else if (!strcmp(key, "sds_uart")) {
            uint32_t cable;
            if (m->sds_uart.attached || !m->intc.attached || !number(comma, &cable) || cable > 1 ||
                !sds_uart_attach(&m->sds_uart, &m->bus, &m->intc, base, (int)cable)) { valid = 0; break; }
            /* The model always carries the port; the bench cable only exists in a
               service session, so a normal boot never loops a byte back. */
            m->sds_uart.cable = sgh_service_mode ? (int)cable : 0;
        } else if (!strcmp(key, "sds_dma")) {
            uint32_t channels, start_delay = 0, byte_time = 0;
            char *next = strchr(comma, ',');
            char *more = next ? strchr(next + 1, ',') : 0;
            if (more) *more++ = 0;
            if (next) *next++ = 0;
            /* The general DMA controller shares its register block with the SIM
               device's channel and only matters for the service loopback, so it
               is left unmapped outside a service session; that keeps a normal
               boot byte-for-byte unchanged. */
            if (!sgh_service_mode) continue;
            if (m->sds_dma.attached || !number(comma, &channels) || !channels ||
                channels > SDS_DMA_MAX_CHANNELS || (next && !number(next, &start_delay)) ||
                (more && !number(more, &byte_time)) ||
                !sds_dma_attach(&m->sds_dma, &m->bus, m->sds_uart.attached ? &m->sds_uart : 0,
                    base, channels, start_delay, byte_time)) { valid = 0; break; }
        } else if (!strcmp(key, "periodic_irq")||!strcmp(key,"periodic_time_irq")) {
            m->periodic_time=!strcmp(key,"periodic_time_irq");
            if (!m->intc.attached || base > 31 || !number(comma, &size) || !size) { valid = 0; break; }
            m->intc.periodic_bit = 1u << base; m->intc.period = size; m->intc.next_tick = size;
        } else if (!strcmp(key, "flash")) {
            char *next = strchr(comma, ',');
            uint32_t device;
            if (!next || m->flash_count == 4) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || !number(next, &device) || size > 65535 || device > 65535 ||
                !flash_attach(&m->flashes[m->flash_count], &m->bus, base, (uint16_t)size, (uint16_t)device)) { valid = 0; break; }
            ++m->flash_count;
        } else if (!strcmp(key, "flash_geometry")) {
            char *next = strchr(comma, ','), *last;
            uint32_t length, block_size;
            unsigned i;
            if (!next) { valid = 0; break; } *next++ = 0;
            last = strchr(next, ',');
            if (!last) { valid = 0; break; } *last++ = 0;
            if (!number(comma, &size) || !number(next, &length) || !number(last, &block_size)) { valid = 0; break; }
            for (i = 0; i < m->flash_count; ++i) if (m->flashes[i].base == base) break;
            if (i == m->flash_count || !flash_geometry(&m->flashes[i], size, length, block_size)) { valid = 0; break; }
        } else if (!strcmp(key, "lcd_s6b33")) {
            char *next = strchr(comma, ',');
            uint32_t origin;
            if (!next || m->lcd.attached) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || !number(next, &origin) || !lcd_attach(&m->lcd, &m->bus, base, size, origin)) { valid = 0; break; }
        } else if (!strcmp(key, "pmic")) {
            if (m->pmic.attached || !number(comma, &size) || !pmic_attach(&m->pmic, &m->bus, base, size)) { valid = 0; break; }
        } else if (!strcmp(key, "pmic_register")) {
            if (!m->pmic.attached || base > 255 || !number(comma, &size) || size > 255) { valid = 0; break; }
            m->pmic.regs[base] = (uint8_t)size;
        } else if (!strcmp(key, "irq_gate")) {
            if ((base & 1) || m->irq_gate_mask || !number(comma, &size) || !size || size > 65535) { valid = 0; break; }
            m->irq_gate_address = base; m->irq_gate_mask = size;
        } else if (!strcmp(key, "register32")||!strcmp(key,"irom_vector")) {
            Register32 *r;
            if ((base & 3) || !number(comma, &size) || m->register_count == SGH_MAX_IDENTITIES) { valid = 0; break; }
            r = &m->registers[m->register_count++]; r->bus = &m->bus; r->address = base; r->value = size;
            r->irom_vector=!strcmp(key,"irom_vector");
            if(r->irom_vector&&(base<0x80000||base>=0x82000||!(size&1))){valid=0;break;}
            if (!bus_device(&m->bus, base, 4, register_read, register_write, r)) { valid = 0; break; }
        } else if (!strcmp(key, "ram")) {
            if (!number(comma, &size) || !size || size > 64u * 1024 * 1024) { valid = 0; break; }
            data = calloc(size, 1);
            if (!data || !bus_map(&m->bus, base, size, data, 0, 1)) { free(data); valid = 0; break; }
        } else if (!strcmp(key, "alias")) {
            char *next = strchr(comma, ',');
            uint32_t source;
            unsigned i;
            int found = 0;
            if (!next) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &source) || !number(next, &size)) { valid = 0; break; }
            for (i = 0; i < m->bus.count; ++i) {
                Region *r = &m->bus.regions[i];
                if (r->data && source >= r->base && (uint64_t)source + size <= (uint64_t)r->base + r->size) {
                    found = bus_map(&m->bus, base, size, r->data + source - r->base, r->readonly, 0); break;
                }
            }
            if (!found) { valid = 0; break; }
        } else if (!strcmp(key, "display")) {
            if (!number(comma, &size) || !base || !size || base > 512 || size > 512) { valid = 0; break; }
            m->width = base; m->height = size;
        } else if (!strcmp(key, "identity16")) {
            Identity16 *id;
            if ((base & 1) || !number(comma, &size) || size > 65535 || m->identity_count == SGH_MAX_IDENTITIES) { valid = 0; break; }
            id = &m->identities[m->identity_count++]; id->bus = &m->bus; id->address = base; id->value = (uint16_t)size;
            if (!bus_device(&m->bus, base, 2, identity_read, identity_write, id)) { valid = 0; break; }
        } else { valid = 0; break; }
    }
    if (ferror(f)||(m->lp_timer.attached&&!m->clock.attached)) valid = 0;
    fclose(f);
    if (valid) {
        unsigned i, j;
        for (i = 0; i < m->irom.count; ++i) {
            IromService *s = &m->irom.services[i];
            if (s->kind == IROM_VERSION && !m->irom.version_configured) valid = 0;
            for (j = 0; j < m->bus.count; ++j) {
                Region *r = &m->bus.regions[j];
                if (s->address >= r->base && (uint64_t)s->address < (uint64_t)r->base + r->size) valid = 0;
            }
        }
    }
    if(valid){unsigned i,j;for(i=0;i<m->register_count;++i)if(m->registers[i].irom_vector){
        for(j=0;j<m->irom.count;++j)if(m->irom.services[j].address==(m->registers[i].value&~1u))break;
        if(j==m->irom.count)valid=0;}}
    if(valid&&m->network.java.first){unsigned i,j;
        if(!m->cellular.ready||!m->cellular.poll)valid=0;
        for(i=0;i<JN_COUNT;++i){int found=0;uint32_t pc=m->network.java.entry[i];
            for(j=0;j<i;++j)if(pc==m->network.java.entry[j])valid=0;
            for(j=0;j<m->rom_count;++j)if(pc>=m->roms[j].base&&
                (uint64_t)pc+2<=(uint64_t)m->roms[j].base+m->roms[j].size)found=1;
            if(!found)valid=0;
        }
    }
    if(valid&&(m->cellular.ready||m->cellular.sms_send||m->network.txn_start||m->cellular.poll)){
        unsigned i,j;uint32_t code[]={m->cellular.ready,m->cellular.alloc,m->cellular.release,
            m->cellular.send_ui,m->cellular.dial,m->cellular.accept,m->cellular.disconnect,
            m->cellular.name_lookup,m->cellular.sms_send,m->cellular.sms_reply,m->cellular.send_message,
            m->cellular.radio_select,m->cellular.alert_request,m->cellular.poll,m->network.txn_start,m->network.send_pdu,m->network.recv_pdu,
            m->network.bearer_state,m->network.post_event,m->network.data_arrived};
        uint32_t ram[]={m->cellular.services,m->cellular.sr_task,m->network.wap_base,m->network.driver_base};
        const unsigned sizes[]={64,4,0x140,20};
        if(!m->cellular.ready)valid=0;
        for(i=0;i<sizeof(code)/sizeof(code[0]);++i){int found=0;if(!code[i])continue;
            for(j=0;j<m->rom_count;++j)if(code[i]>=m->roms[j].base&&
                (uint64_t)code[i]+2<=(uint64_t)m->roms[j].base+m->roms[j].size)found=1;
            if(!found)valid=0;}
        for(i=0;i<4;++i){int found=0;if(!ram[i])continue;
            for(j=0;j<m->bus.count;++j){Region *r=&m->bus.regions[j];
                if(r->data&&!r->readonly&&!r->read&&!r->write&&ram[i]>=r->base&&
                    (uint64_t)ram[i]+sizes[i]<=(uint64_t)r->base+r->size)found=1;}
            if(!found)valid=0;}
    }
    if(valid&&m->java_buffer_get){
        unsigned i,j;uint32_t entries[2]={m->java_buffer_get,m->java_buffer_set};int table=0;
        for(i=0;i<2;++i){int found=0;
            for(j=0;j<m->rom_count;++j)if(entries[i]>=m->roms[j].base&&
                (uint64_t)entries[i]+2<=(uint64_t)m->roms[j].base+m->roms[j].size)found=1;
            if(!found)valid=0;}
        for(i=0;i<m->bus.count;++i){Region *r=&m->bus.regions[i];
            if(r->data&&!r->readonly&&!r->read&&!r->write&&m->java_buffer_table>=r->base&&
                (uint64_t)m->java_buffer_table+3u*0x98u<=(uint64_t)r->base+r->size)table=1;
            if((uint64_t)m->java_buffer_base<(uint64_t)r->base+r->size&&
                (uint64_t)r->base<(uint64_t)m->java_buffer_base+3u*m->java_buffer_size)valid=0;}
        if(!table)valid=0;
    }
    if(valid&&m->battery.enabled&&!m->battery.detect&&!m->battery.charger_port)valid=0;
    if(valid&&m->periodic_time&&!m->clock.attached)valid=0;
    if (valid && m->nvram_size) {
        unsigned i, j;
        for (i = 0; i < m->flash_count; ++i) {
            Flash *flash = &m->flashes[i];
            if (m->nvram_base < flash->base || (uint64_t)m->nvram_base + m->nvram_size > (uint64_t)flash->base + flash->size) continue;
            for (j = 0; j < m->rom_count; ++j) {
                RomImage *image = &m->roms[j];
                if (image->base == flash->base && (uint64_t)m->nvram_base >= (uint64_t)image->base + image->size)
                    m->nvram_data = flash->data + m->nvram_base - flash->base;
            }
        }
        if (!m->nvram_data) valid = 0; /* Never overlay supplied firmware bytes. */
    }
    if (valid && m->nv_trigger) {
        uint32_t entries[3] = {m->nv_trigger, m->nv_format, m->nv_marker}; unsigned i, j;
        if (!m->nvram_data) valid = 0;
        for (i = 0; i < 3; ++i) {
            int found = 0;
            for (j = 0; j < m->rom_count; ++j) if (entries[i] >= m->roms[j].base &&
                (uint64_t)entries[i] + 4 <= (uint64_t)m->roms[j].base + m->roms[j].size) found = 1;
            if (!found) valid = 0;
        }
    }
    if (valid && m->irq_gate_mask) {
        unsigned i;
        for (i = 0; i < m->register_count; ++i) {
            Register32 *r = &m->registers[i];
            if (m->irq_gate_address >= r->address && (uint64_t)m->irq_gate_address + 2 <= (uint64_t)r->address + 4) {
                m->irq_gate_register = r; break;
            }
        }
        if (!m->intc.attached || !m->irq_gate_register) valid = 0;
    }
    if (!valid || !rom || !reset_set || (second_override && !second_used)) {
        snprintf(m->error, sizeof(m->error), "Invalid model or firmware at %.160s:%u", path, line_no);
        bus_destroy(&m->bus); free(m->unplaced_rom); m->unplaced_rom = NULL; m->unplaced_size = 0; return 0;
    }
    m->pixels = calloc((size_t)m->width * m->height, sizeof(*m->pixels));
    if (!m->pixels) { strcpy(m->error, "Cannot allocate LCD framebuffer"); bus_destroy(&m->bus); free(m->unplaced_rom); m->unplaced_rom = NULL; m->unplaced_size = 0; return 0; }
    if (m->lcd.attached) {
        if (m->height > 256 || m->width + m->lcd.origin_y > 256) {
            free(m->pixels); m->pixels = NULL; bus_destroy(&m->bus); free(m->unplaced_rom); m->unplaced_rom = NULL;
            strcpy(m->error, "LCD viewport exceeds controller RAM"); return 0;
        }
        lcd_connect(&m->lcd, m->pixels, m->width, m->height);
    }
    arm7_reset(&m->cpu, &m->bus, m->reset);
    m->decode_cache=arm7_cache_create();m->cpu.cache=m->decode_cache; /* allocation failure uses uncached decoding */
    m->loaded = 1;
    if (!*m->name) strcpy(m->name, "SGH model");
    return 1;
}
/* Only the recovered Java working-buffer helpers are extended. The native
   compiler, file format, registry, flash writer and VM still run unchanged.
   Disabled by default: these larger buffers are an emulator compatibility
   feature, not a claim about the C100's physical memory or package limits. */
static int java_buffer_step(Machine *m)
{
    Arm7 *c=&m->cpu;unsigned handle=c->r[0];uint32_t pc=c->r[15],lr=c->r[14];
    if(!m->java_extended||!m->java_buffer_get||handle>=3||
        (pc!=m->java_buffer_get&&pc!=m->java_buffer_set))return 0;
    if(!(c->cpsr&ARM_T)){c->halted=1;c->error="Java buffer helper: expected Thumb";return -1;}
    if(!m->java_buffer_data){
        uint8_t *data=calloc(3u,m->java_buffer_size);
        if(!data||!bus_map(&m->bus,m->java_buffer_base,3u*m->java_buffer_size,data,0,1)){
            free(data);c->halted=1;c->error="Cannot map extended Java working buffers";return -1;}
        m->java_buffer_data=data;
    }
    if(pc==m->java_buffer_set){
        memset(m->java_buffer_data+(size_t)handle*m->java_buffer_size,0,m->java_buffer_size);
        bus_write(&m->bus,m->java_buffer_table+handle*0x98u+0x94u,4,
            m->java_buffer_base+handle*m->java_buffer_size);
        if(m->bus.fault){c->halted=1;c->error="Java working-buffer table is not writable";return -1;}
        c->r[0]=1;
    }else c->r[0]=m->java_buffer_size;
    c->cpsr=(c->cpsr&~ARM_T)|((lr&1)?ARM_T:0);
    c->r[15]=lr&((lr&1)?~1u:~3u);++c->instructions;++c->cycles;return 1;
}
int machine_step(Machine *m, int continuation)
{
    int hle = 0;
    uint64_t previous_clocks=m->cpu.clock_cycles;
    if (!m->loaded || m->cpu.halted) return 0;
    /* Host queues can only inject at these native scheduler/event-loop entries.
       Do not take the networking thread lock on every interpreted instruction. */
    if(m->cellular.stage||m->cpu.r[15]==m->cellular.ready||
        (m->cellular.poll&&m->cpu.r[15]==m->cellular.poll)){
        if(!m->cellular.boot_ready&&m->sim.card.commands){unsigned i;
            /* This native startup query follows loading the handset UI records,
               also used by first-boot provisioning to recognize settled defaults. */
            for(i=0;i<m->irom.count;++i)if(m->irom.services[i].kind==IROM_VERSION&&m->irom.services[i].calls){
                m->cellular.boot_ready=1;break;
            }
        }
        if(!m->cellular.stage){phone_net_advance(&m->network,&m->cellular);
            java_net_advance(&m->network.java,&m->cellular,&m->cpu,m->network.mode&&m->cellular.enabled);}
        if(!cellular_advance(&m->cellular,&m->cpu,m->guest_busy||m->battery.stage,m->sim.present)){
            m->cpu.error=m->cellular.error;m->cpu.halted=1;return 0;
        }
    }
    if(m->phone_io.output&&!m->phone_io.mixed)phone_io_advance(&m->phone_io,m->clock.nanoseconds);
    if((m->cpu.cpsr&ARM_T)&&(m->cpu.r[15]==m->phone_io.tone_entry||
        m->cpu.r[15]==m->phone_io.tone_stop||m->cpu.r[15]==m->phone_io.gpio_entry))
        phone_io_observe(&m->phone_io,&m->cpu);
    sim_tick(&m->sim,m->cpu.cycles);
    if(m->sds_dma.attached&&m->cpu.cycles>=m->sds_dma.next_event)sds_dma_advance(&m->sds_dma,m->cpu.cycles);
    if(m->ma3.hw)ma3_advance(&m->ma3,m->clock.attached?m->clock.nanoseconds:m->cpu.cycles);
    if (*m->ma3.error) { m->cpu.halted=1; m->cpu.error=m->ma3.error; return 0; }
    if(m->lp_timer.attached&&(m->clock.nanoseconds>=m->lp_timer.next_event||
        ((m->intc.enabled&m->lp_timer.cal_mask)!=m->lp_timer.observed_enabled)))lp_timer_advance(&m->lp_timer);
    if(m->battery.complete_port&&m->clock.nanoseconds>=m->battery.complete_due)battery_advance(&m->battery);
    intc_advance_time(&m->intc, &m->cpu,m->periodic_time?m->clock.nanoseconds:m->cpu.cycles);
    /* Sample the hardware latch, not a synthetic guest bus access. Pending
       sources remain latched while firmware critical sections close the gate. */
    if (m->irq_gate_register && !((m->irq_gate_register->value >>
        ((m->irq_gate_address - m->irq_gate_register->address) * 8)) & m->irq_gate_mask)) m->cpu.irq = 0;
    if (m->bus.fault) { if (!continuation) return 0; m->bus.fault = 0; }
    /* Multi-step host-injected native calls borrow a parked task's CPU state.
       Treat that transaction as atomic: an IRQ-driven task switch can otherwise
       reuse its stack before caller restoration. Device latches/timers continue
       advancing; pending IRQ/FIQ is delivered immediately after the return. */
    if(m->cellular.stage)m->cpu.irq=m->cpu.fiq=0;
    /* Exceptions take precedence over an atomic HLE call, just as over an
       instruction fetch. Never return from the service instead of entering IRQ. */
    if (!((m->cpu.fiq && !(m->cpu.cpsr & 0x40u)) || (m->cpu.irq && !(m->cpu.cpsr & 0x80u))))
        {
            uint32_t pc=m->cpu.r[15];PhoneNetwork *n=&m->network;Cellular *s=&m->cellular;
            /* Keep packet/event stack frames off the interpreter's normal path. */
            if((m->cpu.cpsr&ARM_T)&&((n->txn_start&&
                (pc==n->txn_start||pc==n->send_pdu||pc==n->recv_pdu||pc==n->bearer_state||pc==n->post_event))||
                java_net_may_call(&n->java,pc)))
                hle=phone_net_hle(n,s,&m->cpu);
            if(!hle&&(m->cpu.cpsr&ARM_T)&&s->enabled&&s->ready&&
                (pc==s->radio_select||pc==s->name_lookup||pc==s->dial||pc==s->accept||
                 pc==s->disconnect||pc==s->sms_send||pc==s->sms_reply||pc==s->alert_request))
                hle=cellular_hle(s,&m->cpu);
            if(!hle&&m->java_extended&&(pc==m->java_buffer_get||pc==m->java_buffer_set))hle=java_buffer_step(m);
            if(!hle&&m->battery.enabled&&pc==m->battery.gpio)hle=battery_gpio(&m->battery,&m->cpu);
            if(!hle&&irom_may_call(&m->irom,pc))hle=irom_step(&m->irom,&m->cpu);
        }
    if (hle < 0 || (!hle && !arm7_step(&m->cpu))) return 0;
    if(hle&&m->cpu.clock_cycles==previous_clocks)++m->cpu.clock_cycles; /* Atomic HLE timing remains provisional. */
    if(m->clock.attached)clock_advance(&m->clock,m->cpu.clock_cycles);
    if(m->audio.ops->advance)m->audio.ops->advance(&m->audio, m->cpu.cycles);
    if (m->bus.fault) { if (!continuation) return 0; m->bus.fault = 0; }
    return 1;
}
static void run_stop(uint32_t stops[64],uint32_t pc)
{
    unsigned bit=(pc>>1)&2047u;if(pc)stops[bit>>5]|=1u<<(bit&31);
}
static uint64_t earlier(uint64_t a,uint64_t b){return a<b?a:b;}
unsigned machine_run(Machine *m,unsigned count,int continuation)
{
    if(count<2)return count&&machine_step(m,continuation)?1u:0u;
    uint32_t stops[64]={0};unsigned done=0,i;
    uint32_t hooks[]={m->cellular.ready,m->cellular.poll,m->cellular.radio_select,
        m->cellular.name_lookup,m->cellular.dial,m->cellular.accept,m->cellular.disconnect,
        m->cellular.sms_send,m->cellular.sms_reply,m->cellular.alert_request,
        m->network.txn_start,m->network.send_pdu,m->network.recv_pdu,m->network.bearer_state,
        m->network.post_event,m->phone_io.tone_entry,m->phone_io.tone_stop,m->phone_io.gpio_entry,
        m->java_buffer_get,m->java_buffer_set,m->battery.gpio};
    /* Build from live bindings, not a firmware-specific PC guess. Bloom
       collisions merely take the reference path; they cannot miss a hook. */
    for(i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)run_stop(stops,hooks[i]);
    for(i=0;i<m->irom.count;++i)run_stop(stops,m->irom.services[i].address);
    for(i=0;i<JN_COUNT;++i)run_stop(stops,m->network.java.entry[i]);
    while(done<count){uint32_t epoch=m->bus.device_epoch;uint64_t ns_due=UINT64_MAX,cycle_due=UINT64_MAX;
        if(!machine_step(m,continuation))return done;++done;
        if(epoch!=m->bus.device_epoch||m->cellular.stage||m->battery.stage||m->guest_busy||
           (m->battery.enabled&&!m->battery.adc)||m->audio.ops->advance||m->bus.access_log)continue;
        /* Between callbacks, no device state changes before these deadlines.
           The normal path still delivers events BEFORE the next instruction,
           on precisely the same step as machine_step, even after an overshoot. */
        if(m->sim.attached)cycle_due=earlier(cycle_due,m->sim.next_event);
        if(m->sds_dma.attached)cycle_due=earlier(cycle_due,m->sds_dma.next_event);
        if(m->lp_timer.attached)ns_due=earlier(ns_due,m->lp_timer.next_event);
        if(m->battery.complete_port)ns_due=earlier(ns_due,m->battery.complete_due);
        if(m->phone_io.output&&!m->phone_io.mixed)ns_due=earlier(ns_due,m->phone_io.last_ns+1000000u);
        if(m->ma3.hw){uint64_t due=m->ma3.next_sample;
            if(m->ma3.water)due=earlier(due,m->ma3.water_next);
            if(m->clock.attached)ns_due=earlier(ns_due,due);else cycle_due=earlier(cycle_due,due);
        }
        if(m->intc.attached&&m->intc.period&&(m->intc.enabled&m->intc.periodic_bit)){
            if(m->periodic_time)ns_due=earlier(ns_due,m->intc.next_tick);
            else cycle_due=earlier(cycle_due,m->intc.next_tick);
        }
        while(done<count&&m->clock.nanoseconds<ns_due&&m->cpu.cycles<cycle_due){
            unsigned bit=(m->cpu.r[15]>>1)&2047u;
            if(stops[bit>>5]&(1u<<(bit&31)))break;
            /* The reference controller slides a disabled periodic deadline
               every step. Preserve that state too, not just delivered IRQs. */
            if(m->intc.attached&&m->intc.period&&!(m->intc.enabled&m->intc.periodic_bit))
                m->intc.next_tick=(m->periodic_time?m->clock.nanoseconds:m->cpu.cycles)+m->intc.period;
            if(!arm7_step(&m->cpu))return done;
            if(m->clock.attached)clock_advance(&m->clock,m->cpu.clock_cycles);
            if(m->bus.fault){if(!continuation)return done;m->bus.fault=0;}
            ++done;
            if(epoch!=m->bus.device_epoch)break;
        }
    }
    return done;
}
void machine_key(Machine *m, unsigned key, int down)
{
    if (key >= SGH_MAX_KEYS) return;
    if (down) m->keys |= 1u << key; else m->keys &= ~(1u << key);
    keypad_key(&m->keypad, key, down);
}
void machine_pattern(Machine *m, int enabled)
{
    unsigned x, y;
    if (!m->pixels) return;
    for (y = 0; y < m->height; ++y) for (x = 0; x < m->width; ++x) {
        static const uint32_t colors[] = {0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0};
        m->pixels[(size_t)y * m->width + x] = enabled ? colors[(x * 8 / m->width) & 7] : 0;
    }
}
