/* Compare observable peripheral state after every step, not just final output. */
#include "sim.h"
#include "sds_dma.h"
#include <stdlib.h>
#include <string.h>
#include "peripheral_reference.inc"
static unsigned checks,step;
#define CHECK(x) do { ++checks;if(!(x)){fprintf(stderr,"Peripheral schedule step %u line %d: %s\n",step,__LINE__,#x);exit(1);} } while(0)
typedef struct {Bus bus;Intc intc;Sim sim;SdsUart uart;SdsDma dma;uint8_t ram[512];} Fixture;
static Fixture *old,*now;
static uint32_t rng=0x805c100u;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static Fixture *make(void)
{
    Fixture *f=calloc(1,sizeof(*f));unsigned i;CHECK(f);
    CHECK(bus_map(&f->bus,0x600000,sizeof(f->ram),f->ram,0,0));
    CHECK(intc_attach(&f->intc,&f->bus,0x40100));
    CHECK(sim_attach(&f->sim,&f->bus,&f->intc,0x40a80,0x41300,0x40520));
    CHECK(sds_uart_attach(&f->uart,&f->bus,&f->intc,0x41200,1));
    CHECK(sds_dma_attach(&f->dma,&f->bus,&f->uart,0x40420,8,17,11));
    for(i=0;i<sizeof(f->ram);++i)f->ram[i]=(uint8_t)(i*13);
    return f;
}
static void write_both(uint32_t address,unsigned width,uint32_t value)
{bus_write(&old->bus,address,width,value);bus_write(&now->bus,address,width,value);}
static void compare(void)
{
    unsigned i;
    CHECK(!memcmp(old->ram,now->ram,sizeof(old->ram)));
    CHECK(old->intc.pending==now->intc.pending);
    CHECK(!memcmp(old->sim.regs,now->sim.regs,sizeof(old->sim.regs)));
    CHECK(!memcmp(old->sim.timer,now->sim.timer,sizeof(old->sim.timer)));
    CHECK(!memcmp(old->sim.dma,now->sim.dma,sizeof(old->sim.dma)));
    CHECK(!memcmp(&old->sim.card,&now->sim.card,sizeof(old->sim.card)));
#define SAME(member) CHECK(old->sim.member==now->sim.member)
    SAME(timer_due);SAME(rx_due);SAME(tx_due);SAME(dma_due);SAME(resets);
    SAME(received);SAME(transmitted);SAME(dma_bytes);
#undef SAME
    CHECK(!memcmp(old->uart.regs,now->uart.regs,sizeof(old->uart.regs)));
    CHECK(old->uart.rx==now->uart.rx&&old->uart.rx_ready==now->uart.rx_ready);
    CHECK(old->uart.transmits==now->uart.transmits&&old->uart.receives==now->uart.receives);
    CHECK(old->dma.transfers==now->dma.transfers);
    for(i=0;i<8;++i){SdsDmaChannel *a=&old->dma.channel[i],*b=&now->dma.channel[i];
#define SAME(member) CHECK(a->member==b->member)
        SAME(running);SAME(started);SAME(control);SAME(status);SAME(src);SAME(dst);
        SAME(threshold);SAME(end);SAME(cur_src);SAME(cur_dst);SAME(next_due);SAME(bytes);
#undef SAME
    }
    CHECK(old->bus.fault==now->bus.fault&&old->bus.faults==now->bus.faults);
    CHECK(old->bus.last_address==now->bus.last_address&&old->bus.last_value==now->bus.last_value);
    CHECK(old->bus.last_width==now->bus.last_width&&old->bus.last_write==now->bus.last_write);
}
int main(void)
{
    uint64_t ticks=0;unsigned i;
    old=make();now=make();
    write_both(0x41200,2,1);
    for(i=0;i<8;++i){uint32_t base=0x40420+i*0x40;if(i==4)continue; /* SIM owns this block. */
        write_both(base+4,4,(i&1)?0x4120c:0x600000+i*16);
        write_both(base+8,4,(i&1)?0x600100+i*16:0x41204);
        write_both(base+16,4,((i&1)?0x600100:0x600000)+i*16+15);
        write_both(base,2,(i&1)?9:17);
    }
    for(step=0;step<20000;++step){uint32_t value=random_value();ticks+=1+(value&7);
        old->bus.ticks=now->bus.ticks=ticks;
        if(step%71==0){unsigned ch=(value>>8)&7;uint32_t base=0x40420+ch*0x40;
            if(ch!=4)write_both(base,2,(value&32)?0:(ch&1)?9:17);}
        if(step%97==0)write_both(0x41204,1,value&255); /* Wake stalled RX. */
        if(step%103==0)write_both(0x40a80,2,(value&32)?11:3);
        if(step%107==0)write_both(0x40a86,2,(value&32)?0x18:0);
        if(step%109==0)write_both(0x40a88,1,value&255);
        if(step%113==0){write_both(0x41300,4,1+(value&31));write_both(0x4130c,4,(value&32)?8:0);}
        if(step%127==0)old->sim.present=now->sim.present=!old->sim.present;
        if(step%131==0){
            write_both(0x40520,2,0);write_both(0x40524,4,(value&32)?0x600000:0x40a8a);
            write_both(0x40528,4,(value&32)?0x40a88:0x600100);
            write_both(0x4052c,4,(value&32)?0x60000f:0x60010f);write_both(0x40520,2,(value&32)?17:9);}
        if(step%139==0){ /* Host card reset without any guest MMIO write. */
            sim_card_reset(&old->sim.card);sim_card_reset(&now->sim.card);}
        if(step%149==0)write_both(0x40100,4,UINT32_MAX); /* Status IRQ must reassert. */
        if(step%151==0){unsigned ch=(value>>8)&7;if(ch!=4)write_both(0x40424+ch*0x40,4,0xfffffff0);}
        reference_sim(&old->sim,ticks);sim_tick(&now->sim,ticks);
        reference_dma(&old->dma,ticks);sds_dma_advance(&now->dma,ticks);
        compare();
        old->bus.fault=now->bus.fault=0;
    }
    bus_destroy(&old->bus);bus_destroy(&now->bus);free(old);free(now);
    printf("Peripheral deadline differential: %u checks passed.\n",checks);return 0;
}
