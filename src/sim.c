#include "sim.h"
#include <string.h>
/* Observed CX805 SIM UART/reset timer. Timing is in interpreter ticks. */
static uint32_t mask(unsigned width) { return width == 4 ? UINT32_MAX : (1u << (8 * width)) - 1; }
static void atr(Sim *s)
{
    if (!s->present) return;
    sim_card_reset(&s->card);
    s->rx_due = s->bus->ticks + 1000; ++s->resets;
}
static uint32_t uart_read(void *ctx, uint32_t off, unsigned width)
{
    Sim *s = ctx; uint32_t value;
    if ((width != 1 && width != 2 && width != 4) || (off & 1u) + width > (width == 4 ? 4u : 2u) ||
        (width == 4 && (off & 3u))) {
        bus_fault_access(s->bus,0,s->base+off,width,0,"invalid SIM UART access"); return 0;
    }
    if (off == 10 && s->card.response_size && (s->regs[2] & 8)) {
        uint8_t byte;
        sim_card_receive(&s->card,&byte); value=byte; ++s->received;
        s->regs[2] &= 0xfff7u; s->rx_due = s->bus->ticks + 1000;
        return value;
    }
    value = s->regs[off/2];
    if (width == 4) value |= (uint32_t)s->regs[off/2+1] << 16;
    return (value >> (8 * (off & 1u))) & mask(width);
}
static void uart_write(void *ctx, uint32_t off, unsigned width, uint32_t value)
{
    Sim *s = ctx; uint16_t old = s->regs[0]; unsigned shift = (off & 1u)*8;
    if ((width != 1 && width != 2 && width != 4) || (off & 1u)+width > (width == 4 ? 4u : 2u) ||
        (width == 4 && (off & 3u))) { bus_fault_access(s->bus,1,s->base+off,width,value,"invalid SIM UART access"); return; }
    if (off == 8) { s->tx = (uint8_t)value; s->tx_due=s->bus->ticks+500; s->regs[2]&=0xffefu; ++s->transmitted; }
    s->regs[off/2] = (uint16_t)((s->regs[off/2] & ~(mask(width == 4 ? 2 : width)<<shift)) | (value<<shift));
    if (width == 4) s->regs[off/2+1] = (uint16_t)(value>>16);
    if (off == 0 && !(old & 8) && (s->regs[0] & 8) && (s->regs[0] & 3)==3) atr(s);
    if (off == 0 && (old & 8) && !(s->regs[0] & 8)) { s->card.response_size=0; s->regs[2]=0; }
}
static uint32_t block_read(Sim *s, uint32_t *regs, uint32_t base, uint32_t off, unsigned width)
{
    if ((off & 3u)+width>4) { bus_fault_access(s->bus,0,base+off,width,0,"cross SIM register read"); return 0; }
    return (regs[off/4] >> (8*(off&3u))) & mask(width);
}
static void block_write(Sim *s, uint32_t *regs, uint32_t base, uint32_t off, unsigned width,uint32_t value)
{
    uint32_t bits;
    if ((off & 3u)+width>4) { bus_fault_access(s->bus,1,base+off,width,value,"cross SIM register write"); return; }
    bits=mask(width)<<(8*(off&3u)); regs[off/4]=(regs[off/4]&~bits)|((value<<(8*(off&3u)))&bits);
}
static uint32_t timer_read(void *ctx,uint32_t off,unsigned width) { Sim *s=ctx; return block_read(s,s->timer,s->timer_base,off,width); }
static void timer_write(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    Sim *s=ctx; block_write(s,s->timer,s->timer_base,off,width,value);
    s->timer_due=(s->timer[3]&8) ? 0 : s->bus->ticks+(uint64_t)(s->timer[0]?s->timer[0]:1)*4u;
}
static uint32_t dma_read(void *ctx,uint32_t off,unsigned width) { Sim *s=ctx; return block_read(s,s->dma,s->dma_base,off,width); }
static void dma_write(void *ctx,uint32_t off,unsigned width,uint32_t value)
{
    Sim *s=ctx; uint32_t old=s->dma[0]; block_write(s,s->dma,s->dma_base,off,width,value);
    if(off==0 && !(old&1) && (s->dma[0]&1)) {
        s->dma[5]=s->dma[1];s->dma[6]=s->dma[2];s->dma_due=s->bus->ticks+1000;
    }
}
int sim_attach(Sim *s,Bus *bus,Intc *intc,uint32_t base,uint32_t timer,uint32_t dma)
{
    unsigned count=bus->count;
    if (s->attached || !intc->attached || (base&3) || (timer&3) || (dma&3)) return 0;
    memset(s,0,sizeof(*s)); s->bus=bus;s->intc=intc;s->base=base;s->timer_base=timer;s->dma_base=dma;
    /* Existing cold-handoff fixture, retained until its source is understood. */
    s->regs[0]=0x7420;s->regs[1]=0x2001;s->timer[3]=8;
    sim_card_init(&s->card);s->card.response_size=0;
    if (!bus_device(bus,base,16,uart_read,uart_write,s) || !bus_device(bus,timer,16,timer_read,timer_write,s) ||
        !bus_device(bus,dma,32,dma_read,dma_write,s)) { bus->count=count; return 0; }
    s->attached=1;return 1;
}
void sim_advance(Sim *s,uint64_t ticks)
{
    if (!s->attached) return;
    if (s->timer_due && ticks>=s->timer_due) { s->timer_due=0;s->intc->pending|=1; }
    if (s->tx_due && ticks>=s->tx_due) { s->tx_due=0;s->regs[2]|=0x10;if(s->present)sim_card_send(&s->card,s->tx); }
    if (s->present && s->card.response_size && ticks>=s->rx_due && (s->regs[0]&0xb)==0xb) s->regs[2]|=8;
    if (s->present && (s->dma[0]&1) && ticks>=s->dma_due) {
        uint32_t ptr,mode=s->dma[0]&0x18;
        unsigned i; Region *r=NULL; uint8_t byte;
        ptr=mode==0x10?s->dma[5]:s->dma[6];
        for(i=0;i<s->bus->count;++i) if(s->bus->regions[i].data && ptr>=s->bus->regions[i].base &&
            (uint64_t)ptr<(uint64_t)s->bus->regions[i].base+s->bus->regions[i].size) {r=&s->bus->regions[i];break;}
        if(!r || (mode!=0x10 && r->readonly) || (mode!=8 && mode!=0x10)) {
            bus_fault_access(s->bus,mode!=0x10,ptr,1,0,"invalid SIM DMA buffer/mode");s->dma[0]&=~1u;
        } else if(mode==0x10 || (s->regs[2]&8)) {
            if(mode==0x10) {byte=(uint8_t)bus_read(s->bus,ptr,1);sim_card_send(&s->card,byte);++s->transmitted;++s->dma[5];}
            else {sim_card_receive(&s->card,&byte);bus_write(s->bus,ptr,1,byte);++s->received;++s->dma[6];s->regs[2]&=0xfff7u;}
            ++s->dma_bytes;s->dma_due=ticks+1000;s->rx_due=ticks+1000;
            if(ptr==s->dma[3]) {s->intc->pending|=0x400;s->dma[0]&=~1u;}
            if(mode==0x10 && ptr==s->dma[3])s->regs[2]|=0x10;
        }
    }
    if (s->regs[2]&s->regs[3]&0x18) s->intc->pending|=8;
}
