#include "battery.h"
static void complete_level(Battery *b,unsigned level)
{
    gpio_port_input(b->complete_port,b->complete_bit,(int)level);
    if(level!=b->complete){b->complete_intc->pending|=1u<<b->complete_irq;++b->complete_intc->events;}
    b->complete=level;
}
static void charger_update(Battery *b)
{
    if(!b->complete_port)return;
    if(!b->charging||b->percent<100){complete_level(b,0);b->complete_due=UINT64_MAX;return;}
    /* Keep charge-done asserted after the firmware disables charging. A new
       low battery/cable removal clears it. The enable pin is active-low. */
    if(b->complete)return;
    if(b->complete_port->output&(1u<<b->enable_bit)){b->complete_due=UINT64_MAX;return;}
    if(b->complete_due==UINT64_MAX)b->complete_due=*b->nanoseconds+(uint64_t)b->complete_delay_ms*1000000u;
}
void battery_charger_output(void *context,unsigned mask,unsigned value,uint64_t ticks)
{
    Battery *b=context;(void)value;(void)ticks;
    if(mask&(1u<<b->enable_bit))charger_update(b);
}
void battery_advance(Battery *b)
{
    if(!b->complete_port||*b->nanoseconds<b->complete_due)return;
    b->complete_due=UINT64_MAX;
    if(b->charging&&b->percent==100&&!(b->complete_port->output&(1u<<b->enable_bit)))complete_level(b,1);
}
static void restore(Battery *b,Arm7 *c)
{
    uint64_t steps=c->instructions,cycles=c->cycles,clocks=c->clock_cycles;
    *c=b->caller;c->instructions=steps;c->cycles=cycles;c->clock_cycles=clocks;b->stage=0;
}
void battery_set(Battery *b,unsigned percent,int charging)
{
    if(percent>100)percent=100;
    if(b->adc){
        unsigned raw=b->adc_empty+((b->adc_full-b->adc_empty)*percent+50u)/100u;
        dsp_mailbox_inputs(b->adc,raw,b->adc_temperature);b->level_pending=0;
    }else if(b->percent!=percent)b->level_pending=1;
    if(b->charger_port){
        gpio_port_input(b->charger_port,b->charger_bit,!charging);
        /* The cable input is active-low. Latch transitions in the hardware
           controller; native firmware performs debounce and notification. */
        if(b->charging!=(unsigned)!!charging){
            b->charger_intc->pending|=1u<<b->charger_irq;++b->charger_intc->events;
        }
        b->cable_pending=0;
    }else if(b->charging!=(unsigned)!!charging)b->cable_pending=1;
    b->percent=percent;b->charging=(unsigned)!!charging;
    charger_update(b);
}
int battery_gpio(Battery *b,Arm7 *c)
{
    uint32_t pin;
    if(b->charger_port||!b->enabled||c->r[15]!=b->gpio||!(c->cpsr&ARM_T)||
       !bus_peek(c->bus,b->pin,1,&pin)||c->r[0]!=pin)return 0;
    /* Verified active-low TA detection. Only this GPIO query is intercepted. */
    c->last_pc=c->r[15];c->last_opcode=0;c->bus->pc=c->last_pc;c->bus->ticks=c->cycles;
    bus_write(c->bus,c->r[1],1,b->charging?0:1);c->r[0]=0;
    c->cpsr=(c->cpsr&~ARM_T)|((c->r[14]&1)?ARM_T:0);
    c->r[15]=c->r[14]&((c->r[14]&1)?~1u:~3u);++c->instructions;++c->cycles;return 1;
}
int battery_step(Battery *b,Arm7 *c)
{
    /* A dedicated return address cannot be confused with an RTOS idle visit. */
    if(b->adc||!b->enabled||c->instructions<300000000||
       c->r[15]!=(b->stage?0xffff0000u:b->ready))return 1;
    if(!(c->cpsr&ARM_T))return 0;
    switch(b->stage){
    case 0:
        if(!b->level_pending&&!b->cable_pending)return 1;
        b->caller=*c;
        if(b->cable_pending){b->cable_pending=0;c->r[0]=c->r[1]=c->r[2]=c->r[3]=0;
            c->r[15]=b->detect;b->stage=3;break;}
        b->level_pending=0;b->voltage=3300+b->percent*9; /* Explicit provisional SOC -> mV curve. */
        c->r[0]=4;c->r[15]=b->alloc;b->stage=1;break;
    case 1:
        if(!c->r[0]){restore(b,c);return 0;}
        bus_write(c->bus,c->r[0],2,b->voltage);bus_write(c->bus,c->r[0]+2,2,25);
        c->r[15]=b->report;b->stage=2;break;
    default:restore(b,c);return 1;
    }
    c->r[14]=0xffff0001u;return 1;
}
