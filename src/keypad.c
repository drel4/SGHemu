#include "keypad.h"
#include <string.h>
static uint16_t sense(const Keypad *d)
{
    uint16_t value = 0xffff;
    unsigned i;
    for (i = 0; i < 32; ++i) if ((d->held & d->mapped & (1u << i)) &&
        !(d->drive & (1u << (8 + d->row[i])))) value &= (uint16_t)~(1u << d->column[i]);
    return value;
}
static int access(Keypad *d, uint32_t offset, unsigned width, int write, uint32_t value)
{
    if ((width != 1 && width != 2) || (offset & 1u) + width > 2 || offset >= 6) {
        bus_fault_access(d->bus, write, d->base + offset, width, value, "invalid keypad register access"); return 0;
    }
    return 1;
}
static uint32_t read_keypad(void *ctx, uint32_t offset, unsigned width)
{
    Keypad *d = ctx;
    uint16_t value;
    if (!access(d, offset, width, 0, 0)) return 0;
    value = offset < 2 ? d->control : offset < 4 ? d->drive : sense(d);
    ++d->reads;
    return (value >> (8 * (offset & 1))) & (width == 1 ? 255u : 65535u);
}
static void write_keypad(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Keypad *d = ctx;
    uint16_t *target, mask;
    unsigned shift = 8 * (offset & 1);
    if (!access(d, offset, width, 1, value)) return;
    if (offset >= 4) { bus_fault_access(d->bus, 1, d->base + offset, width, value, "read-only keypad columns"); return; }
    target = offset < 2 ? &d->control : &d->drive;
    mask = (uint16_t)((width == 1 ? 255u : 65535u) << shift);
    *target = (uint16_t)((*target & ~mask) | ((value << shift) & mask)); ++d->writes;
}
int keypad_attach(Keypad *d, Bus *bus, uint32_t base, unsigned rows, unsigned columns)
{
    if (d->attached || (base & 1) || !rows || rows > 8 || !columns || columns > 5) return 0;
    memset(d, 0, sizeof(*d)); d->bus = bus; d->base = base; d->rows = (uint8_t)rows; d->columns = (uint8_t)columns;
    d->drive = 0xffff;
    if (!bus_device(bus, base, 6, read_keypad, write_keypad, d)) return 0;
    d->attached = 1; return 1;
}
int keypad_map(Keypad *d, unsigned key, unsigned row, unsigned column)
{
    if (!d->attached || key >= 32 || row >= d->rows || column >= d->columns || (d->mapped & (1u << key))) return 0;
    /* Host aliases may share a switch. Each held bit remains independent. */
    d->mapped |= 1u << key; d->row[key] = (uint8_t)row; d->column[key] = (uint8_t)column; return 1;
}
void keypad_key(Keypad *d, unsigned key, int down)
{
    if (key >= 32 || !(d->mapped & (1u << key))) return;
    if (down) d->held |= 1u << key; else d->held &= ~(1u << key);
}
