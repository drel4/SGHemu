#include "intc.h"
#include <string.h>
static uint32_t read_intc(void *ctx, uint32_t offset, unsigned width)
{
    Intc *d = ctx;
    uint32_t result;
    unsigned reg = offset & ~3u;
    if ((offset & 3u) + width > 4) {
        bus_fault_access(d->bus, 0, d->base + offset, width, 0, "cross-register interrupt read"); return 0;
    }
    switch (reg) {
    case 0: result = d->pending; break;
    case 8: result = d->enabled; break;
    case 12: result = d->fiq_select; break;
    case 20: result = d->pending & d->enabled & d->fiq_select; break;
    case 24: result = d->pending & d->enabled & ~d->fiq_select; break;
    case 4: case 16: case 40: result = d->control[reg / 4]; break;
    default: bus_fault_access(d->bus, 0, d->base + offset, width, 0, "unimplemented interrupt register"); return 0;
    }
    result >>= (offset & 3u) * 8;
    return width == 4 ? result : result & ((1u << (width * 8)) - 1);
}
static void write_intc(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Intc *d = ctx;
    uint32_t *target, mask, shift = (offset & 3u) * 8;
    if ((offset & 3u) + width > 4) {
        bus_fault_access(d->bus, 1, d->base + offset, width, value, "cross-register interrupt write"); return;
    }
    mask = width == 4 ? UINT32_MAX : ((1u << (width * 8)) - 1) << shift;
    switch (offset & ~3u) {
    case 0: d->pending &= ~((value << shift) & mask); return;
    case 8: target = &d->enabled; break;
    case 12: target = &d->fiq_select; break;
    case 4: case 16: case 40: target = &d->control[offset / 4]; break;
    default: bus_fault_access(d->bus, 1, d->base + offset, width, value, "unimplemented interrupt register"); return;
    }
    *target = (*target & ~mask) | ((value << shift) & mask);
}
int intc_attach(Intc *d, Bus *bus, uint32_t base)
{
    memset(d, 0, sizeof(*d)); d->bus = bus; d->base = base;
    if (!bus_device(bus, base, 0x40, read_intc, write_intc, d)) return 0;
    d->attached = 1; return 1;
}
void intc_advance_time(Intc *d, Arm7 *cpu,uint64_t ticks)
{
    if (!d->attached) return;
    if (d->period) {
        if (!(d->enabled & d->periodic_bit)) d->next_tick = ticks + d->period;
        else if (ticks >= d->next_tick) {
            d->pending |= d->periodic_bit; ++d->events;
            d->next_tick += ((ticks-d->next_tick)/d->period+1)*d->period;
        }
    }
    cpu->irq = !!(d->pending & d->enabled & ~d->fiq_select);
    cpu->fiq = !!(d->pending & d->enabled & d->fiq_select);
    if(d->secondary){Intc *s=d->secondary;
        cpu->irq|=!!(s->pending&s->enabled&~s->fiq_select);
        cpu->fiq|=!!(s->pending&s->enabled&s->fiq_select);}
}
void intc_advance(Intc *d,Arm7 *cpu){intc_advance_time(d,cpu,cpu->cycles);}
