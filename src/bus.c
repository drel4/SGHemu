#include "bus.h"
#include <stdlib.h>
#include <string.h>
static Region *find(Bus *b, uint32_t a, unsigned width)
{
    unsigned i;
    if (width != 1 && width != 2 && width != 4) return NULL;
    for (i = 0; i < b->count; ++i) {
        Region *r = &b->regions[i];
        if (a >= r->base && (uint64_t)a + width <= (uint64_t)r->base + r->size) return r;
    }
    return NULL;
}
static int add(Bus *b, uint32_t base, uint32_t size)
{
    unsigned i;
    if (!size || (uint64_t)base + size > UINT64_C(0x100000000) || b->count == BUS_MAX_REGIONS) return 0;
    for (i = 0; i < b->count; ++i) {
        Region *r = &b->regions[i];
        if ((uint64_t)base < (uint64_t)r->base + r->size && (uint64_t)r->base < (uint64_t)base + size) return 0;
    }
    memset(&b->regions[b->count], 0, sizeof(Region));
    b->regions[b->count].base = base; b->regions[b->count].size = size;
    ++b->count; return 1;
}
int bus_map(Bus *b, uint32_t base, uint32_t size, uint8_t *data, int readonly, int owned)
{
    Region *r;
    if (!data || !add(b, base, size)) return 0;
    r = &b->regions[b->count - 1]; r->data = data; r->readonly = readonly; r->owned = owned;
    return 1;
}
int bus_device(Bus *b, uint32_t base, uint32_t size, PeripheralRead read, PeripheralWrite write, void *ctx)
{
    Region *r;
    if (!read || !write || !add(b, base, size)) return 0;
    r = &b->regions[b->count - 1]; r->read = read; r->write = write; r->context = ctx; return 1;
}
static void fault(Bus *b, char op, uint32_t a, unsigned width, uint32_t v, const char *why)
{
    b->fault = 1; ++b->faults;
    b->last_address = a; b->last_value = v; b->last_width = width; b->last_write = op == 'W';
    if (b->log) fprintf(b->log, "%llu PC=%08x %c%u %08x value=%08x %s\n",
        (unsigned long long)b->ticks, b->pc, op, width * 8, a, v, why);
}
void bus_fault_access(Bus *b, int write, uint32_t address, unsigned width, uint32_t value, const char *reason)
{
    fault(b, write ? 'W' : 'R', address, width, value, reason);
}
static void watch(Bus *b, char op, uint32_t address, unsigned width, uint32_t value, const Region *r)
{
    if (b->access_log && address >= b->watch_start && address < b->watch_end)
        fprintf(b->access_log, "%llu PC=%08x %c%u %08x value=%08x %s\n",
            (unsigned long long)b->ticks, b->pc, op, width * 8, address, value,
            !r ? "unmapped" : r->read ? "device" : r->readonly ? "ROM" : "RAM");
}
uint32_t bus_read(Bus *b, uint32_t a, unsigned width)
{
    Region *r = find(b, a, width);
    uint32_t value = 0;
    unsigned i;
    if (!r) fault(b, 'R', a, width, 0, "unmapped");
    else if (r->read) value = r->read(r->context, a - r->base, width);
    else for (i = 0; i < width; ++i) value |= (uint32_t)r->data[a - r->base + i] << (8 * i);
    watch(b, 'R', a, width, value, r);
    return value;
}
void bus_write(Bus *b, uint32_t a, unsigned width, uint32_t value)
{
    Region *r = find(b, a, width);
    unsigned i;
    watch(b, 'W', a, width, value, r);
    if (!r) { fault(b, 'W', a, width, value, "unmapped"); return; }
    if (r->write) { r->write(r->context, a - r->base, width, value); return; }
    if (r->readonly) { fault(b, 'W', a, width, value, "ROM write"); return; }
    for (i = 0; i < width; ++i) r->data[a - r->base + i] = (uint8_t)(value >> (8 * i));
}
void bus_destroy(Bus *b)
{
    unsigned i;
    for (i = 0; i < b->count; ++i) if (b->regions[i].owned) free(b->regions[i].data);
    b->count = 0;
}
