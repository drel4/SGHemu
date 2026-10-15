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
    uint64_t next_event;
    unsigned scheduled_response;
    int scheduled_present;
    SimCard card;
    uint8_t tx;
    int attached, present;
} Sim;
int sim_attach(Sim *s, Bus *bus, Intc *intc, uint32_t base, uint32_t timer, uint32_t dma);
void sim_advance(Sim *s, uint64_t ticks);
/* Keep the no-event path inline in the machine loop. IRQ status remains
   level-like even when INTC was acknowledged after the last timed event. */
static inline void sim_tick(Sim *s,uint64_t ticks)
{
    if(!s->attached)return;
    if(ticks>=s->next_event || s->present!=s->scheduled_present ||
        s->card.response_size!=s->scheduled_response)sim_advance(s,ticks);
    else if(s->regs[2]&s->regs[3]&0x18)s->intc->pending|=8;
}
#endif
