#ifndef SGH_GPIO_H
#define SGH_GPIO_H
#include "intc.h"
/* CX805 eight-pin data port: low byte is the write mask, high byte the value.
   External inputs override the output latch. Direction/mux registers are not
   implemented here; this device models the data port only. */
typedef struct {
    Bus *bus;
    uint32_t base;
    uint8_t output, inputs, driven;
    int attached;
    void (*output_changed)(void *context,unsigned mask,unsigned value,uint64_t ticks);
    void *output_context;
} GpioPort;
int gpio_port_attach(GpioPort *p,Bus *bus,uint32_t base,unsigned reset);
int gpio_port_input(GpioPort *p,unsigned pin,int high);
#endif
