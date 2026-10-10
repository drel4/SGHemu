#ifndef SGH_INTC_H
#define SGH_INTC_H
#include "arm7.h"
typedef struct {
    Bus *bus;
    uint32_t base, pending, enabled, fiq_select, control[16], periodic_bit;
    uint64_t period, next_tick, events;
    int attached;
} Intc;
int intc_attach(Intc *device, Bus *bus, uint32_t base);
void intc_advance(Intc *device, Arm7 *cpu);
void intc_advance_time(Intc *device,Arm7 *cpu,uint64_t ticks);
#endif
