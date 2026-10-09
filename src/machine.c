#include "machine.h"
#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
void machine_destroy(Machine *m)
{
    if (m->audio.ops) m->audio.ops->stop(&m->audio);
    bus_destroy(&m->bus); free(m->pixels); free(m->unplaced_rom); memset(m, 0, sizeof(*m));
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
    machine_destroy(m);
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
        if (!strcmp(key, "name")) { if (strlen(value) >= sizeof(m->name)) { valid = 0; break; } strcpy(m->name, value); continue; }
        if (!strcmp(key, "reset")) { if (!number(value, &m->reset)) { valid = 0; break; } reset_set = 1; continue; }
        if (!strcmp(key, "audio")) { if (strcmp(value, "silent")) { valid = 0; break; } continue; }
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
        } else if (!strcmp(key, "sim")) {
            char *next=strchr(comma,','); uint32_t dma;
            if (!next) { valid=0; break; } *next++=0;
            if (!number(comma,&size) || !number(next,&dma) || !sim_attach(&m->sim,&m->bus,&m->intc,base,size,dma)) { valid=0; break; }
        } else if (!strcmp(key, "nv_provision")) {
            char *next = strchr(comma, ','); uint32_t marker;
            if (!next || m->nv_trigger || !base || (base & 1)) { valid = 0; break; } *next++ = 0;
            if (!number(comma, &size) || !size || (size & 1) || !number(next, &marker) || !marker || (marker & 1)) { valid = 0; break; }
            m->nv_trigger = base; m->nv_format = size; m->nv_marker = marker;
        } else if (!strcmp(key, "nvram")) {
            if (m->nvram_size || !number(comma, &size) || !size || (base & 1) || (size & 1)) { valid = 0; break; }
            m->nvram_base = base; m->nvram_size = size;
        } else if (!strcmp(key, "periodic_irq")) {
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
        } else if (!strcmp(key, "register32")) {
            Register32 *r;
            if ((base & 3) || !number(comma, &size) || m->register_count == SGH_MAX_IDENTITIES) { valid = 0; break; }
            r = &m->registers[m->register_count++]; r->bus = &m->bus; r->address = base; r->value = size;
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
    if (ferror(f)) valid = 0;
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
    arm7_reset(&m->cpu, &m->bus, m->reset); m->loaded = 1;
    if (!*m->name) strcpy(m->name, "SGH model");
    return 1;
}
int machine_step(Machine *m, int continuation)
{
    int hle = 0;
    if (!m->loaded || m->cpu.halted) return 0;
    sim_advance(&m->sim,m->cpu.cycles);
    intc_advance(&m->intc, &m->cpu);
    /* Sample the hardware latch, not a synthetic guest bus access. Pending
       sources remain latched while firmware critical sections close the gate. */
    if (m->irq_gate_register && !((m->irq_gate_register->value >>
        ((m->irq_gate_address - m->irq_gate_register->address) * 8)) & m->irq_gate_mask)) m->cpu.irq = 0;
    if (m->bus.fault) { if (!continuation) return 0; m->bus.fault = 0; }
    /* Exceptions take precedence over an atomic HLE call, just as over an
       instruction fetch. Never return from the service instead of entering IRQ. */
    if (!((m->cpu.fiq && !(m->cpu.cpsr & 0x40u)) || (m->cpu.irq && !(m->cpu.cpsr & 0x80u))))
        hle = irom_step(&m->irom, &m->cpu);
    if (hle < 0 || (!hle && !arm7_step(&m->cpu))) return 0;
    m->audio.ops->advance(&m->audio, m->cpu.cycles);
    if (m->bus.fault) { if (!continuation) return 0; m->bus.fault = 0; }
    return 1;
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
