#include "pmic.h"
#include <string.h>
/* CX805 serial pins: bit 0 is output, bit 1 is the sampled line.
   The observed PMIC protocol sends address+direction, then register index,
   then data, without a repeated START between index and read. */
static unsigned input(const Pmic *p)
{
    if (p->active && p->ack) return p->selected ? 0u : 1u;
    if (p->active && p->selected && p->reading && p->stage >= 2) {
        unsigned bit = p->bits;
        if ((p->lines[1] & 1) && bit) --bit;
        if (bit < 8) return (p->regs[p->index] >> (7 - bit)) & 1;
    }
    return p->lines[0] & 1;
}
static uint32_t read_lines(void *ctx, uint32_t offset, unsigned width)
{
    Pmic *p = ctx;
    uint32_t v;
    if (width != 2 || offset > 2) {
        bus_fault_access(p->bus, 0, p->base + offset, width, 0, "PMIC pins require aligned x16 access"); return 0;
    }
    v = p->lines[offset / 2] & ~2u;
    return v | ((offset ? p->lines[1] & 1 : input(p)) << 1);
}
static void write_lines(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Pmic *p = ctx;
    unsigned old_sda = p->lines[0] & 1, old_scl = p->lines[1] & 1;
    unsigned sda, scl;
    if (width != 2 || offset > 2) {
        bus_fault_access(p->bus, 1, p->base + offset, width, value, "PMIC pins require aligned x16 access"); return;
    }
    p->lines[offset / 2] = (uint16_t)(value & ~2u);
    sda = p->lines[0] & 1; scl = p->lines[1] & 1;
    if (!offset && old_scl && old_sda != sda) {
        if (!sda) {
            p->active = 1; p->bits = p->byte = p->stage = p->ack = p->selected = 0;
        } else p->active = 0;
        return;
    }
    if (!p->active || !offset || old_scl == scl) return;
    if (scl) {
        if (p->ack) return;
        if (!(p->reading && p->stage >= 2)) p->byte = (p->byte << 1) | sda;
        ++p->bits;
    } else if (p->bits == 8 && !p->ack) {
        if (p->stage == 0) { p->selected = (p->byte & 254) == p->address; p->reading = p->byte & 1; }
        else if (p->stage == 1) p->index = p->byte;
        else if (p->selected && !p->reading) p->regs[p->index++] = (uint8_t)p->byte;
        p->ack = 1;
    } else if (p->ack) {
        if (p->stage >= 2 && p->reading) ++p->index;
        ++p->stage; p->bits = p->byte = p->ack = 0; ++p->transfers;
    }
}
int pmic_attach(Pmic *p, Bus *bus, uint32_t base, unsigned address)
{
    if ((base & 1) || address > 254 || (address & 1)) return 0;
    memset(p, 0, sizeof(*p)); p->bus = bus; p->base = base; p->address = address;
    p->lines[0] = p->lines[1] = 1;
    if (!bus_device(bus, base, 4, read_lines, write_lines, p)) return 0;
    p->attached = 1; return 1;
}
