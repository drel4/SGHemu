#include "gpio.h"
#include <string.h>
static uint32_t read_port(void *ctx,uint32_t offset,unsigned width)
{
    GpioPort *p=ctx;
    if(offset||width!=2){bus_fault_access(p->bus,0,p->base+offset,width,0,"GPIO data port requires x16 access");return 0;}
    return (p->output&~p->driven)|(p->inputs&p->driven);
}
static void write_port(void *ctx,uint32_t offset,unsigned width,uint32_t value)
{
    GpioPort *p=ctx;unsigned mask=value&255u;
    if(offset||width!=2){bus_fault_access(p->bus,1,p->base+offset,width,value,"GPIO data port requires x16 access");return;}
    p->output=(uint8_t)((p->output&~mask)|((value>>8)&mask));
    if(p->output_changed)p->output_changed(p->output_context,mask,p->output,p->bus->ticks);
}
int gpio_port_attach(GpioPort *p,Bus *bus,uint32_t base,unsigned reset)
{
    if(p->attached||(base&1)||reset>255)return 0;
    memset(p,0,sizeof(*p));p->bus=bus;p->base=base;p->output=(uint8_t)reset;
    if(!bus_device(bus,base,2,read_port,write_port,p))return 0;
    p->attached=1;return 1;
}
int gpio_port_input(GpioPort *p,unsigned pin,int high)
{
    unsigned mask;
    if(!p||!p->attached||pin>=8)return 0;
    mask=1u<<pin;p->driven|=(uint8_t)mask;
    p->inputs=(uint8_t)((p->inputs&~mask)|(high?mask:0));return 1;
}
