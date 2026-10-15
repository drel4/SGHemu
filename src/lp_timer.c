#include "lp_timer.h"
#include <string.h>
#define COUNTER_MASK 0x3ffffu
static uint64_t ticks(const LpTimer *d)
{uint64_t ns=*d->nanoseconds;return ns/1000000000u*d->hz+(ns%1000000000u)*d->hz/1000000000u;}
static uint64_t tick_time(const LpTimer *d,uint64_t tick)
{return tick/d->hz*1000000000u+((tick%d->hz)*1000000000u+d->hz-1)/d->hz;}
static int valid(LpTimer *d,uint32_t off,unsigned width,int write,uint32_t value)
{
    if((width!=1&&width!=2&&width!=4)||off>=8||width>8-off){
        bus_fault_access(d->bus,write,d->base+off,width,value,"invalid low-power timer access");return 0;}return 1;
}
static uint32_t read_timer(void *ctx,uint32_t off,unsigned width)
{
    LpTimer *d=ctx;uint64_t value;
    if(!valid(d,off,width,0,0))return 0;
    /* +2 is the unimplemented fine phase. Coarse snapshots are coherent and
       advance at the independent low-power oscillator, not the ARM clock. */
    value=(uint64_t)d->captured<<32|d->control;
    return (uint32_t)(value>>(off*8))&(width==4?UINT32_MAX:(1u<<(width*8))-1);
}
static void write_timer(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    LpTimer *d=ctx;unsigned i;uint16_t control=d->control;uint32_t compare=d->compare;
    int control_written=0,compare_written=0;
    if(!valid(d,off,width,1,value))return;
    for(i=0;i<width;++i){unsigned at=off+i,byte=(value>>(i*8))&255u;
        if(at<2){unsigned shift=at*8;control=(uint16_t)((control&~(255u<<shift))|(byte<<shift));control_written=1;}
        else if(at>=4){unsigned shift=(at-4)*8;compare=(compare&~(255u<<shift))|(byte<<shift);compare_written=1;}}
    if(control_written){d->control=control;if(!(control&2)){d->captured=(uint32_t)ticks(d)&COUNTER_MASK;d->armed=0;}}
    if(compare_written){d->compare=compare&COUNTER_MASK;
        if(d->control&2){uint64_t now=ticks(d);uint32_t delta=(d->compare-(uint32_t)now)&COUNTER_MASK;
            d->wake_due=tick_time(d,now+delta);d->armed=1;}}
    d->next_event=0;
}
int lp_timer_attach(LpTimer *d,Bus *bus,Intc *intc,const uint64_t *ns,
    uint32_t base,uint32_t hz,unsigned cal_irq,unsigned wake_irq,uint32_t period_ns)
{
    if(d->attached||!intc->attached||!ns||(base&3)||!hz||!period_ns||cal_irq>=32||wake_irq>=32||cal_irq==wake_irq)return 0;
    memset(d,0,sizeof(*d));d->bus=bus;d->intc=intc;d->nanoseconds=ns;d->base=base;d->hz=hz;
    d->cal_mask=1u<<cal_irq;d->wake_mask=1u<<wake_irq;d->period=period_ns;
    if(!bus_device(bus,base,8,read_timer,write_timer,d))return 0;d->attached=1;return 1;
}
void lp_timer_advance(LpTimer *d)
{
    uint64_t now;uint32_t enabled;
    if(!d->attached)return;
    now=*d->nanoseconds;enabled=d->intc->enabled&d->cal_mask;
    if(enabled!=d->observed_enabled){d->next_cal=enabled?now+d->period:UINT64_MAX;d->observed_enabled=enabled;}
    if(enabled&&now>=d->next_cal){d->intc->pending|=d->cal_mask;++d->cal_events;
        d->next_cal+=((now-d->next_cal)/d->period+1)*d->period;}
    if(d->armed&&now>=d->wake_due){d->armed=0;d->intc->pending|=d->wake_mask;++d->wake_events;}
    d->next_event=enabled?d->next_cal:UINT64_MAX;
    if(d->armed&&d->wake_due<d->next_event)d->next_event=d->wake_due;
}
