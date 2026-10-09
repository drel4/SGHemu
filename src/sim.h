#ifndef SGH_SIM_H
#define SGH_SIM_H
#include "intc.h"
#include "sim_card.h"
typedef struct {
    Bus *bus;
    Intc *intc;
    uint32_t base, timer_base, dma_base;
    uint16_t regs[8];
    uint32_t timer[4], dma[8];
    uint64_t timer_due, rx_due, tx_due, dma_due, resets, received, transmitted, dma_bytes;
    SimCard card;
    uint8_t tx;
    int attached, present;
} Sim;
int sim_attach(Sim *s, Bus *bus, Intc *intc, uint32_t base, uint32_t timer, uint32_t dma);
void sim_advance(Sim *s, uint64_t ticks);
#endif
