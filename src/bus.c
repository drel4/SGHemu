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
static void build_pages(Bus *b)
{
    unsigned i;
    uint32_t p;
    memset(b->page, 0, sizeof(b->page));
    for (i = 0; i < b->count; ++i) {
        const Region *r = &b->regions[i];
        uint64_t first = ((uint64_t)r->base + ((1u << BUS_PAGE_SHIFT) - 1)) >> BUS_PAGE_SHIFT;
        uint64_t end = ((uint64_t)r->base + r->size) >> BUS_PAGE_SHIFT;
        /* Regions never overlap, so a page wholly inside one is exclusively its. */
        for (p = (uint32_t)first; p < end; ++p) b->page[p] = (uint8_t)(i + 1);
    }
    b->pages_ready = 1;
}
/* Same result as find(), but O(1) for the large ROM/RAM regions that dominate
   instruction fetch and data access. Small devices fall through to find(). */
static Region *lookup(Bus *b, uint32_t a, unsigned width)
{
    unsigned index;
    if (width > 4 || !((0x16u >> width) & 1u)) return NULL;
    if (!b->pages_ready) build_pages(b);
    index = b->page[a >> BUS_PAGE_SHIFT];
    if (index) {
        Region *r = &b->regions[index - 1];
        if ((uint64_t)a + width <= (uint64_t)r->base + r->size) return r;
    }
    return find(b, a, width);
}
static int add(Bus *b, uint32_t base, uint32_t size)
{
    unsigned i;
    b->pages_ready = 0;
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
    Region *r = lookup(b, a, width);
    uint32_t value = 0;
    if (!r) fault(b, 'R', a, width, 0, "unmapped");
    else if (r->read && !r->fast) value = r->read(r->context, a - r->base, width);
    else {
        const uint8_t *p = r->data + (a - r->base); /* little-endian host (x86) */
        if (width == 4) memcpy(&value, p, 4);
        else if (width == 2) { uint16_t half; memcpy(&half, p, 2); value = half; }
        else value = p[0];
    }
    if (b->access_log) watch(b, 'R', a, width, value, r);
    return value;
}
void bus_write(Bus *b, uint32_t a, unsigned width, uint32_t value)
{
    Region *r = lookup(b, a, width);
    if (b->access_log) watch(b, 'W', a, width, value, r);
    if (!r) { fault(b, 'W', a, width, value, "unmapped"); return; }
    if (r->write) { r->write(r->context, a - r->base, width, value); return; }
    if (r->readonly) { fault(b, 'W', a, width, value, "ROM write"); return; }
    {
        uint8_t *p = r->data + (a - r->base);
        if (width == 4) memcpy(p, &value, 4);
        else if (width == 2) { uint16_t half = (uint16_t)value; memcpy(p, &half, 2); }
        else p[0] = (uint8_t)value;
    }
}
void bus_destroy(Bus *b)
{
    unsigned i;
    for (i = 0; i < b->count; ++i) if (b->regions[i].owned) free(b->regions[i].data);
    b->count = 0; b->pages_ready = 0;
}
