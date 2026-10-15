#ifndef SGH_LP_TIMER_H
#define SGH_LP_TIMER_H
#include "intc.h"
typedef struct {
    Bus *bus;Intc *intc;const uint64_t *nanoseconds;
    uint32_t base,hz,cal_mask,wake_mask,observed_enabled,captured,compare;
    uint16_t control;
    uint64_t period,next_cal,wake_due,next_event,cal_events,wake_events;
    int attached,armed;
} LpTimer;
int lp_timer_attach(LpTimer *d,Bus *bus,Intc *intc,const uint64_t *ns,
    uint32_t base,uint32_t hz,unsigned cal_irq,unsigned wake_irq,uint32_t period_ns);
void lp_timer_advance(LpTimer *d);
#endif
