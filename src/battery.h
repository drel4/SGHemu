#ifndef SGH_BATTERY_H
#define SGH_BATTERY_H
#include "arm7.h"
#include "gpio.h"
#include "dsp_mailbox.h"
/* Physical ADC/GPIO inputs plus optional legacy guest-call compatibility.
   Native firmware handles measurement processing, UI and charging. */
typedef struct {
    uint32_t ready,alloc,report,detect,gpio,pin;
    unsigned percent,charging,voltage;
    int enabled,level_pending,cable_pending,stage;
    Arm7 caller;
    GpioPort *charger_port;
    Intc *charger_intc;
    unsigned charger_bit,charger_irq;
    DspMailbox *adc;
    unsigned adc_empty,adc_full,adc_temperature;
    GpioPort *complete_port;Intc *complete_intc;
    uint64_t *nanoseconds,complete_due;
    unsigned complete_bit,enable_bit,complete_irq,complete_delay_ms,complete;
} Battery;
void battery_set(Battery *b,unsigned percent,int charging);
int battery_gpio(Battery *b,Arm7 *cpu);
int battery_step(Battery *b,Arm7 *cpu);
void battery_charger_output(void *context,unsigned mask,unsigned value,uint64_t ticks);
void battery_advance(Battery *b);
#endif
