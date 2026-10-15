#include "dsp_mailbox.h"
#include <string.h>
#define REQUEST_WORDS 349u
#define RESPONSE_START 352u
static void unsupported(DspMailbox *d,unsigned opcode)
{
    ++d->unsupported;
    if(!d->warned[opcode]){
        if(d->bus->log)fprintf(d->bus->log,"DSP mailbox: unsupported opcode %02x (no synthesized response)\n",opcode);
        /* Do not consume the diagnostic when logging is disabled. */
        if(d->bus->log)d->warned[opcode]=1;
    }
}
static void exchange(DspMailbox *d)
{
    unsigned at=1,end=1u+d->words[0],out=0;
    ++d->transactions;
    /* Publish the preceding transaction's results. The native six-phase ADC
       scan expects a pipeline, not an immediate result from the current read. */
    memcpy(d->words+RESPONSE_START,d->pending,d->pending_count*sizeof(uint16_t));
    d->words[350]=(uint16_t)d->pending_count;d->words[351]=0;
    d->pending_count=0;
    if(end>1u+REQUEST_WORDS)goto malformed;
    while(at<end){unsigned header=d->words[at++],n=header&31u,opcode=header>>8;
        if(!header)break;
        if(n>end-at)goto malformed;
        if(header==0x0202&&d->words[at]==0x4814)d->mux=d->words[at+1];
        else if(header==0x09c2&&d->words[at]==0x52e4){
            unsigned source=(d->mux>>5)&7u,value;
            if(source!=0&&source!=5&&source!=7){unsupported(d,opcode);at+=n;continue;}
            value=source==0?d->battery_raw:source==5?d->temperature_raw:0;
            if(out+3>350)goto malformed;
            d->pending[out++]=(uint16_t)header;d->pending[out++]=0x52e4;
            d->pending[out++]=(uint16_t)value;++d->conversions;
        }else unsupported(d,opcode);
        at+=n;
    }
    d->pending_count=out;return;
malformed:
    ++d->malformed;d->pending_count=0;
    bus_fault_access(d->bus,1,d->control_address,2,d->control,"malformed DSP mailbox request");
}
static int access(DspMailbox *d,uint32_t off,unsigned width,unsigned size,int write,uint32_t value,uint32_t base)
{
    if((width!=1&&width!=2&&width!=4)||off>=size||width>size-off){
        bus_fault_access(d->bus,write,base+off,width,value,"invalid DSP mailbox access");return 0;}
    return 1;
}
static uint32_t read_window(void *ctx,uint32_t off,unsigned width)
{
    DspMailbox *d=ctx;uint32_t value=0;
    if(access(d,off,width,sizeof(d->words),0,0,d->base))memcpy(&value,(uint8_t *)d->words+off,width);
    return value;
}
static void write_window(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    DspMailbox *d=ctx;
    if(access(d,off,width,sizeof(d->words),1,value,d->base))memcpy((uint8_t *)d->words+off,&value,width);
}
static uint32_t read_control(void *ctx,uint32_t off,unsigned width)
{
    DspMailbox *d=ctx;
    if(!access(d,off,width,2,0,0,d->control_address))return 0;
    return (d->control>>(off*8))&(width==1?255u:65535u);
}
static void write_control(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    DspMailbox *d=ctx;unsigned mask=width==1?255u:65535u;
    if(!access(d,off,width,2,1,value,d->control_address))return;
    d->control=(uint16_t)((d->control&~(mask<<(off*8)))|((value&mask)<<(off*8)));
    if(d->control&1)exchange(d);
    /* Synchronous mailbox completion. Exact DSP busy duration is unknown. */
    d->control&=0xffeeu;
}
int dsp_mailbox_attach(DspMailbox *d,Bus *bus,uint32_t base,uint32_t control)
{
    unsigned before=bus->count;
    if(d->attached||(base&1)||(control&1))return 0;
    memset(d,0,sizeof(*d));d->bus=bus;d->base=base;d->control_address=control;
    d->battery_raw=138;d->temperature_raw=20;
    if(!bus_device(bus,base,sizeof(d->words),read_window,write_window,d)||
       !bus_device(bus,control,2,read_control,write_control,d)){
        /* No partially attached device may survive a failed config load. */
        bus->count=before;bus->pages_ready=0;++bus->map_generation;return 0;
    }
    d->attached=1;return 1;
}
void dsp_mailbox_inputs(DspMailbox *d,unsigned battery,unsigned temperature)
{
    if(battery<=255&&temperature<=255){d->battery_raw=battery;d->temperature_raw=temperature;}
}
