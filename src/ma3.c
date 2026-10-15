#include "ma3.h"
#include "ma_water.h"
#include <string.h>
static void reset_parameters(Ma3 *m)
{
    /* MA-3 parameter defaults explicitly checked by VerifyRegisters. These
       differ from MA-5; model real state, not a forced success return. */
    memset(m->parameter_ram,0,sizeof(m->parameter_ram));
    memset(m->parameter_ram+0x90,0x60,16);memset(m->parameter_ram+0xa0,0x3c,16);
    memset(m->parameter_ram+0x14a,0x40,8);
    /* Color history belongs to the host indicator, not MA parameter RAM.
       Keep it through chip resets (for example when switching songs). */
    m->led_token=m->led_on=0;m->led_tick=0;
    m->packet_phase=m->address_bytes=m->length_bytes=m->response_mask=0;
    m->packet_address=m->packet_length=0;
    m->delayed_length=m->delayed_phase=m->delayed_address=m->delayed_count=m->delayed_count_bytes=0;
    m->sequence_divider=m->sequence_valid=0;
    m->water_sample_burst=0;
}
static void sequence_clock(Ma3 *m)
{
    unsigned divider=m->sequence_divider;
    if(!m->hw||!m->sequence_valid)return;
    if(m->water){divider=(divider*(m->water_scale?m->water_scale:100)+50)/100;
        if(divider<64)divider=64;if(divider>16383)divider=16383;}
    ma5_hw_register(m->hw,0x23,divider>>6,m->bank);
    ma5_hw_register(m->hw,0x24,(divider&63)<<2,m->bank);
    /* Water can run between guest port accesses, including a register read
       or a RAM packet. The helper leaves native selector 4 selected; restore
       the guest's selection before its next data access. */
    ma5_hw_write(m->hw,0,m->selector);
}
static unsigned water_voice(Ma3 *m,unsigned address,unsigned value)
{
    unsigned voice=address<0x90?address:address>=0xe0&&address<0x140?address-0xe0:UINT32_MAX;
    /* Leave voice selection and note-gate fields alone; damage pitch and
       expression payloads without redirecting a voice to arbitrary RAM. */
    if(m->water&&voice!=UINT32_MAX&&voice%6>=2&&voice%6<=4){
        uint32_t r=ma_water_random(&m->water_rng);
        if((r&7u)==0){value^=1u<<((r>>8)%6);++m->water_faults;}
    }
    return value; /* Keep terminator/channel bits and all address/length bytes. */
}
static unsigned water_sample(Ma3 *m,unsigned value)
{
    unsigned result=value;
    if(m->water){result=ma_water_sample_byte(&m->water_rng,&m->water_sample_burst,value);
        if(result!=value)++m->water_faults;}
    return result;
}
static unsigned translate_address(unsigned address)
{
    if(address<0x90)return address;
    if(address<0x140)return address+0x30;
    /* Native 1DE is a stored no-op. Use it as an exclusively owned LED marker
       so the DLL sequences the toggle at its original delta, not FIFO receipt. */
    if(address==0x146)return 0x1de;
    /* Native bits 2/3 correspond to MA-3 SoftInt0/1. Translate ISR separately.
       Keep payloads in the delayed FIFO, not immediate host callbacks. */
    if(address>=0x15a&&address<=0x15d)return address+0x7c;
    /* Explicit no-op commands emitted by MaSndDrv_Nop. */
    if(address==0x148||address==0x166)return 0x1df;
    return UINT32_MAX;
}
static unsigned translate_value(unsigned address,unsigned value)
{
    /* C100 GetFmBlockFnum/GetWtBlockFnum pack FNUM[9:7],BLOCK,FNUM[6:0].
       MA-5 expects BLOCK,FNUM[9:7],FNUM[6:0] (Hw decoder 100036B8).
       Swap only voice pitch high bytes; preserve independent-channel bit 6
       and the packet terminator bit 7. Voice RAM is not parameter RAM. */
    unsigned offset=address<0x90?address:address>=0xe0&&address<0x140?address-0xe0:UINT32_MAX;
    if(offset!=UINT32_MAX&&offset%6==3)
        return (value&0xc0)|((value&7)<<3)|((value>>3)&7);
    return value;
}
static unsigned native_irq_low(unsigned value)
{
    return ((value&3)<<2)|((value&0x60)>>1)|(value&0x80);
}
static unsigned guest_irq(unsigned low,unsigned high)
{
    return ((low&0x0c)>>2)|(high&0x0c)|((low&0x30)<<1)|(low&0x80);
}
static unsigned led_marker(Ma3 *m,unsigned value)
{
    m->led_serial=(m->led_serial+1)&63;
    return (value&128)|(m->led_serial<<1)|(value&1);
}
static void led_update(Ma3 *m)
{
    unsigned token=ma5_hw_led_token(m->hw);static const unsigned colors[]={2,4,1,3};
    if(token==m->led_token)return;
    m->led_token=token;m->led_on=token&1;m->led_tick=m->bus->ticks;++m->led_events;
    if(m->led_on){unsigned x=m->led_rng?m->led_rng:0x6d2b79f5u;
        unsigned choices[4],count=0,i;
        /* Keep the last lit color across off events. Choose directly from
           the remaining palette so consecutive on events cannot repeat. */
        for(i=0;i<4;++i)if(colors[i]!=m->led_color)choices[count++]=colors[i];
        x^=x<<13;x^=x>>17;x^=x<<5;m->led_rng=x;m->led_color=choices[x%count];}
}
static void immediate_parameter(Ma3 *m,unsigned address,unsigned value)
{
    unsigned mapped=translate_address(address);
    if(!m->hw)return;
    if(mapped!=UINT32_MAX){ma5_hw_parameter(m->hw,mapped,address==0x146?led_marker(m,value):translate_value(address,water_voice(m,address,value)));++m->translated;}
    else if(address==0x161){
        ma5_hw_register(m->hw,0x26,value&3,m->bank);
        /* SeekControl starts, polls the FIFO sixteen times, then stops/clears.
           Waiting for the next PCM block loses its zero-delay setup commands.
           Execute due commands now without generating samples or time. */
        if(value&1)ma5_hw_pump(m->hw);
        ++m->translated;
    }
    else if(address==0x15e){++m->translated; /* High 7-bit divider field, latch on low write. */}
    else if(address==0x15f){
        /* C100 SetSpeed packs (divider >> 7), (divider & 127), not MA-5's
           eight-bit register layout. Its conversion constant is 1157 ns;
           the pinned MA-5 counter uses 1085 ns per divider unit. */
        unsigned divider=((unsigned)m->parameter_ram[0x15e]<<7)|(value&127);
        unsigned native=(divider*m->sequence_quantum_ns+542u)/1085u;
        if(native>16383){strcpy(m->error,"MA-3 sequence divider exceeds MA-5 range");return;}
        m->sequence_divider=native;m->sequence_valid=1;sequence_clock(m);++m->translated;
    }
    else if(address==0x160){ma5_hw_register(m->hw,0x25,value&127,m->bank);++m->translated;}
    /* MA-3 secondary timer: C100 programs prescale 27 / reload 20, and
       Timer0 drives stream refill plus the secondary software sequencer.
       Native independent Timer A uses 37109 ns * prescale * reload. */
    else if(address==0x152){ma5_hw_register(m->hw,0x2e,value&127,m->bank);++m->translated;}
    else if(address==0x154){ma5_hw_register(m->hw,0x2f,value&127,m->bank);++m->translated;}
    else if(address==0x155){ma5_hw_register(m->hw,0x30,value&3,m->bank);++m->translated;}
    /* MA-3 FIFO IRQ is controlled by global IEN; seven parameter bits control
       its other sources. MA-5 exposes FIFO enable in bit 7 of this register. */
    else if(address==0x162){
        ma5_hw_register(m->hw,0x11,native_irq_low(value)|128,m->bank);
        ma5_hw_register(m->hw,0x12,value&0x0c,m->bank);++m->translated;
    }
    else ++m->unsupported;
}
static void delayed_packet(Ma3 *m,unsigned value)
{
    unsigned i,mapped;value&=255;
    if(m->delayed_length==sizeof(m->delayed)){strcpy(m->error,"MA-3 delayed packet exceeds bound");++m->unsupported;return;}
    m->delayed[m->delayed_length++]=(uint8_t)value;
    if(m->delayed_phase==0) {
        if(value&128){m->delay_end=m->delayed_length;m->delayed_phase=1;m->address_count=0;}
    } else if(m->delayed_phase==1) {
        if(m->address_count>=3){strcpy(m->error,"Invalid MA-3 delayed address");return;}
        m->delayed_address|=(value&127)<<(7*m->address_count++);
        if(value&128){m->address_end=m->delayed_length;m->delayed_phase=m->address_count==3?2:3;}
    } else if(m->delayed_phase==2) {
        if(m->delayed_count_bytes>=2){strcpy(m->error,"Invalid MA-3 delayed length");return;}
        m->delayed_count|=(value&127)<<(7*m->delayed_count_bytes++);
        if(value&128)m->delayed_phase=m->delayed_count?4:5;
    } else if(m->delayed_phase==3) {if(value&128)m->delayed_phase=5;}
    else if(m->delayed_phase==4) {if(m->delayed_count)--m->delayed_count;if(!m->delayed_count)m->delayed_phase=5;}
    if(m->delayed_phase!=5)return;
    mapped=m->address_count==3?m->delayed_address:translate_address(m->delayed_address);
    if(mapped==UINT32_MAX){++m->unsupported;mapped=0x1df;}
    if(m->hw) {
        uint8_t packet[sizeof(m->delayed)+3];unsigned length=0;
        for(i=0;i<m->delay_end;++i)packet[length++]=m->delayed[i];
        if(m->address_count==3){for(i=m->delay_end;i<m->address_end;++i)packet[length++]=m->delayed[i];}
        else {
            if(mapped>=128){packet[length++]=(uint8_t)(mapped&127);packet[length++]=(uint8_t)((mapped>>7)|128);}
            else packet[length++]=(uint8_t)(mapped|128);
        }
        /* Preserve the delay of an unsupported command with a hardware NOP. */
        if(mapped==0x1df)packet[length++]=128;
        else for(i=m->address_end;i<m->delayed_length;++i)
            packet[length++]=(uint8_t)(m->address_count==3?
                (i<m->address_end+m->delayed_count_bytes?m->delayed[i]:water_sample(m,m->delayed[i])):
                (m->delayed_address+i-m->address_end==0x146?led_marker(m,m->delayed[i]):
                 translate_value(m->delayed_address+i-m->address_end,
                    water_voice(m,m->delayed_address+i-m->address_end,m->delayed[i]))));
        ma5_hw_fifo(m->hw,1,packet,length);
        ++m->translated;
    }
    m->delayed_phase=m->delayed_length=m->delayed_address=m->delayed_count=m->delayed_count_bytes=0;
}
static void parameter_packet(Ma3 *m,unsigned value)
{
    unsigned mode=m->selector&63;value&=255;
    if(mode!=2&&mode!=3)return;
    if(mode==2&&m->packet_phase==3){m->packet_phase=m->address_bytes=m->length_bytes=0;m->packet_address=m->packet_length=0;}
    if(m->packet_phase==0) {
        if(m->address_bytes>=3){strcpy(m->error,"Invalid MA-3 immediate address");m->packet_phase=3;return;}
        m->packet_address|=(value&127)<<(7*m->address_bytes++);
        if(value&128) {
            m->packet_phase=mode==3?2:m->address_bytes==3?1:2;
            if(mode==2&&m->address_bytes==3&&m->hw) {
                m->packet_start=m->packet_address;
                ma5_hw_write(m->hw,0,2);ma5_hw_write(m->hw,1,m->packet_address&127);
                ma5_hw_write(m->hw,1,(m->packet_address>>7)&127);ma5_hw_write(m->hw,1,(m->packet_address>>14)|128);
            }
        }
    } else if(m->packet_phase==1) {
        if(m->length_bytes>=2){strcpy(m->error,"Invalid MA-3 immediate length");m->packet_phase=3;return;}
        m->packet_length|=(value&127)<<(7*m->length_bytes++);
        if(m->hw)ma5_hw_write(m->hw,1,value);
        if(value&128){m->packet_total=m->packet_length;m->packet_phase=m->packet_length?2:3;}
    } else if(m->packet_phase==2) {
        if(mode==3) {
            /* MA-3 RBO is selected by the request's low seven bits. The
               hardware DLL exposes only RBO1; MA-3 parameter reads use RBO3. */
            unsigned mapped=translate_address(m->packet_address);
            if(m->hw&&(value&127)==0&&(m->address_bytes==3||mapped!=UINT32_MAX)) {
                m->response=ma5_hw_memory(m->hw,m->address_bytes==3?m->packet_address:mapped,m->address_bytes==3);
                if(m->address_bytes!=3)m->response=translate_value(m->packet_address,m->response);
                m->response_mask=0x10;m->response_selector=1;++m->ram_reads;
            } else if(m->hw&&(value&127)==0&&m->packet_address>=0x14a&&m->packet_address<=0x151) {
                unsigned cursor=ma5_hw_pcm_cursor(m->hw,m->packet_address-0x14a);
                unsigned format=cursor>>24,offset=cursor&0xffff;
                if(cursor==UINT32_MAX){strcpy(m->error,"Native PCM cursor layout is unavailable");return;}
                offset=format==0?offset/2:format==1?offset*2:offset;
                offset=(offset+0x20)&0x3ff;
                m->response=0x40|(format<2?(offset>>4)&0x3e:(offset>>5)&0x1f);
                m->response_mask=0x10;m->response_selector=1;++m->ram_reads;
            } else if(m->packet_address<sizeof(m->parameter_ram)&&(value&127)<=2) {
                m->response=m->parameter_ram[m->packet_address];m->response_mask=1u<<((value&127)+4);
                m->response_selector=(value&127)+1;++m->ram_reads;
            }
            m->packet_phase=3;
        } else {
            if(m->packet_address<sizeof(m->parameter_ram))m->parameter_ram[m->packet_address]=(uint8_t)(m->address_bytes==3?value:value&127);
            if(m->address_bytes==3){if(m->hw)ma5_hw_write(m->hw,1,water_sample(m,value));}
            else immediate_parameter(m,m->packet_address,value);
            ++m->packet_address;
            if(m->address_bytes==3){
                if(m->packet_length)--m->packet_length;
                if(!m->packet_length){
                    m->packet_phase=3;
                    if(m->hw)ma5_hw_ram_changed(m->hw,m->packet_start,m->packet_total);
                }
            }
            else if(value&128)m->packet_phase=3;
        }
    }
}
static uint32_t read_ma3(void *ctx,uint32_t off,unsigned width)
{
    Ma3 *m=ctx;unsigned result=0;
    if(width!=1||(off!=0&&off!=4)){bus_fault_access(m->bus,0,m->base+off,width,0,"MA-3 requires byte port access");return 0;}
    ++m->reads;if(m->hw)result=ma5_hw_read(m->hw,off==4);
    if(m->hw&&off==4&&(m->selector&63)==0&&m->bank==0) {
        unsigned high;
        ma5_hw_write(m->hw,0,0x10);high=ma5_hw_read(m->hw,1);
        ma5_hw_write(m->hw,0,m->selector);result=guest_irq(result,high);
    }
    if(m->hw&&off==0)result|=m->response_mask;
    /* Bits 4..6 acknowledge the selected read buffer, not its number.
       ReceiveData selects RBO1/2/3 with 0x11/0x22/0x43. */
    if(m->hw&&off==4&&(m->selector&15)==m->response_selector&&m->response_mask){result=m->response;m->response_mask=0;}
    if(m->trace)fprintf(m->trace,"%llu %08x R %u %02x sel=%02x\n",(unsigned long long)m->bus->ticks,m->bus->pc,off,result,m->selector);
    return result;
}
static void write_ma3(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    Ma3 *m=ctx;unsigned reset_sequence=0,clear_fifo=0;
    if(width!=1||(off!=0&&off!=4)){bus_fault_access(m->bus,1,m->base+off,width,value,"MA-3 requires byte port access");return;}
    ++m->writes;if(off==0) {
        m->selector=value&255;
        if((m->selector&63)==2||(m->selector&63)==3){m->packet_phase=m->address_bytes=m->length_bytes=0;m->packet_address=m->packet_length=0;}
    }
    else if((m->selector&63)==1||(m->selector&63)==2)++m->fifo_bytes;
    if(off==4) {
        if((m->selector&63)==4){
            reset_sequence=(value&~m->control)&8;
            clear_fifo=(value&~m->control)&0x20;
            if(value&128)reset_parameters(m);m->bank=value&3;m->control=value&255;
        }
        if(clear_fifo) {
            /* The chip's FIFO, and any half-received packet in it, is gone. The
               translation layer assembles packets before handing them to the
               DLL, so it must forget its partial packet too; otherwise the first
               bytes of the next tune are glued onto the interrupted one and the
               opening instrument/tempo setup is misparsed or lost. */
            ++m->fifo_clears;if(m->delayed_phase||m->delayed_length)++m->partial_drops;
            m->delayed_phase=m->delayed_length=m->delayed_address=m->delayed_count=m->delayed_count_bytes=0;
        }
        if((m->selector&63)==1)delayed_packet(m,value);
        parameter_packet(m,value);
        if(m->hw&&(m->selector&63)==7&&m->bank==0) {
            /* Experimental shared output/FM/PCM gain mapping. MA-5 resets
               its synth gains to zero too. The MA-3 analog curve and separate
               synth controls still need reverse engineering. Zero is mute. */
            unsigned gain=(value&31)?(((value&31)*15/31)<<3)|2:0;
            ma5_hw_register(m->hw,0x27,gain,m->bank);ma5_hw_register(m->hw,0x28,gain,m->bank);
            ma5_hw_register(m->hw,0x29,gain,m->bank);ma5_hw_register(m->hw,0x2a,gain,m->bank);
        }
    }
    if(m->trace)fprintf(m->trace,"%llu %08x W %u %02x sel=%02x\n",(unsigned long long)m->bus->ticks,m->bus->pc,off,value&255,m->selector);
    if(m->hw) {
        if(off==0)ma5_hw_write(m->hw,0,value);
        else if((m->selector&63)==0&&m->bank==0) {
            ma5_hw_register(m->hw,0x10,value&0x0c,m->bank);
            ma5_hw_write(m->hw,0,m->selector);ma5_hw_write(m->hw,1,native_irq_low(value));
        }
        else if((m->selector&63)>3||(m->selector&63)==0) {
            ma5_hw_write(m->hw,0,m->selector);ma5_hw_write(m->hw,1,value);
            /* The raw reset above rebuilds the DLL engine and returns it to
               16 voices. This must follow that write, not precede it. */
            if((m->selector&63)==4&&(value&128))ma5_hw_register(m->hw,0x38,1,m->bank);
            /* C100 ClearFifo uses 0x28; refill recovery uses clear 0x20,
               then reset 0x08 before restarting. Native MA-5 bit 3 only
               resets its parser, leaving the cumulative tick/deadline pair
               from the interrupted tune. Rebase both through native reset,
               preserving the guest run bit and independent refill timer.
               Plain stop/start (pause/resume) must not reset sequence time. */
            if(reset_sequence)ma5_hw_register(m->hw,0x26,(m->parameter_ram[0x161]&1)|2,m->bank);
        }
    }
}
int ma3_attach(Ma3 *m,Bus *bus,Intc *intc,uint32_t base,uint32_t irq_mask,uint32_t sequence_quantum_ns)
{
    if(m->attached||!intc->attached||(base&3)||!irq_mask||!sequence_quantum_ns||sequence_quantum_ns>10000)return 0;
    memset(m,0,sizeof(*m));m->bus=bus;m->intc=intc;m->base=base;m->irq_mask=irq_mask;
    m->sequence_quantum_ns=sequence_quantum_ns;
    if(!bus_device(bus,base,8,read_ma3,write_ma3,m))return 0;reset_parameters(m);m->attached=1;return 1;
}
int ma3_enable(Ma3 *m,const char *dll,const char *wav,int speakers,uint32_t ticks_hz)
{
    if(!m->attached||m->hw||ticks_hz<48000||ticks_hz>1000000000)return 0;
    m->hw=ma5_hw_open(dll,wav,speakers,m->error,sizeof(m->error));if(!m->hw)return 0;
    /* The DLL's FM engine starts at sixteen voices (its constructor gets
       param 1 -> this+0x1804 = 0x10, FUN_10015670), and key-ons for
       voice >= that count are silently dropped (FUN_10016094). The MA-3
       reaches its second FM bank through native voices 16..31
       (0x110..0x16F), so those notes would vanish entirely at the
       default count. Register 0x38 bit0=1 selects the 32-voice bank
       (case 0x38 -> FUN_10016198(engine,~v&1): 0 -> 32 voices). */
    ma5_hw_register(m->hw,0x38,1,0);
    m->ticks_hz=ticks_hz;m->last_tick=m->bus->ticks;m->fractional=m->pending_frames=0;
    ma5_hw_set_water(m->hw,m->water);return 1;
}
void ma3_set_water(Ma3 *m,int enabled)
{
    if(m->water==!!enabled)return;
    m->water=!!enabled;m->water_scale=100;m->water_next=0;
    m->water_sample_burst=0;
    ma5_hw_set_water(m->hw,m->water);sequence_clock(m);
}
int ma3_set_block(Ma3 *m,unsigned frames)
{
    if(frames<16||frames>480||480%frames)return 0;
    m->block_frames=frames;return 1;
}
void ma3_advance(Ma3 *m,uint64_t ticks)
{
    uint64_t scaled;
    unsigned block=m->block_frames?m->block_frames:480;
    if(!m->hw)return;
    if(m->water&&ticks>=m->water_next){uint32_t r=ma_water_random(&m->water_rng);
        m->water_scale=25+r%301;sequence_clock(m);++m->water_faults;
        m->water_next=ticks+(uint64_t)m->ticks_hz*(25+(r>>16)%136)/1000;}
    if(ticks>=m->next_sample) {
      scaled=m->fractional+(ticks-m->last_tick)*48000;m->last_tick=ticks;
      if(scaled<m->ticks_hz)m->fractional=scaled;
      else {
        m->pending_frames+=scaled/m->ticks_hz;m->fractional=scaled%m->ticks_hz;
        /* The backend stages short blocks into 10 ms sound-card buffers. */
        while(m->pending_frames>=block){unsigned count=block;
            if(!ma5_hw_generate(m->hw,count)){strcpy(m->error,"Hardware PCM generation/output failed");return;}m->pending_frames-=count;}
      }
      m->next_sample=ticks+(m->ticks_hz-m->fractional+47999)/48000;
    }
    led_update(m);
    if(ma5_hw_irq(m->hw)){m->intc->pending|=m->irq_mask;++m->irq_events;}
}
void ma3_destroy(Ma3 *m)
{
    if(m->hw)ma5_hw_close(m->hw);if(m->trace)fclose(m->trace);memset(m,0,sizeof(*m));
}
