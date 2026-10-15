#include "arm7.h"
#include "flash.h"
#include "sds_dma.h"
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"CPU cache line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t dynamic_opcode;static unsigned fetches;
static uint32_t fetch(void *ctx,uint32_t offset,unsigned width)
{(void)ctx;(void)offset;(void)width;++fetches;return dynamic_opcode;}
static void discard(void *ctx,uint32_t offset,unsigned width,uint32_t value)
{(void)ctx;(void)offset;(void)width;(void)value;}
static void step_at(Arm7 *c,uint32_t pc,int thumb)
{c->r[15]=pc;c->cpsr=(c->cpsr&~ARM_T)|(thumb?ARM_T:0);CHECK(arm7_step(c));}
static int expected_condition(unsigned cc,unsigned flags)
{
    int n=!!(flags&8),z=!!(flags&4),cy=!!(flags&2),v=!!(flags&1);
    switch(cc){case 0:return z;case 1:return !z;case 2:return cy;case 3:return !cy;
    case 4:return n;case 5:return !n;case 6:return v;case 7:return !v;
    case 8:return cy&&!z;case 9:return !cy||z;case 10:return n==v;case 11:return n!=v;
    case 12:return !z&&n==v;default:return z||n!=v;}
}
int main(void)
{
    Bus *b=calloc(1,sizeof(*b));Arm7 c;Arm7Cache *cache=arm7_cache_create();
    uint8_t ram[1024]={0},rom[256]={0};Flash flash={0};SdsDma dma={0};FILE *log;char line[256];unsigned i,reads=0;
    CHECK(b&&cache);CHECK(bus_map(b,0,sizeof(ram),ram,0,0));
    CHECK(bus_map(b,0x1000,sizeof(ram),ram,0,0)); /* shared writable alias */
    CHECK(bus_map(b,0x2000,sizeof(rom),rom,1,0));
    CHECK(flash_attach(&flash,b,0x2000,0x2107,0x1234));
    CHECK(bus_device(b,0x3000,4,fetch,discard,NULL));
    CHECK(sds_dma_attach(&dma,b,NULL,0x4000,1,17,11));
    arm7_reset(&c,b,0);c.cache=cache;
    bus_write(b,0,4,0xe3a00001);step_at(&c,0,0);CHECK(c.r[0]==1);
    step_at(&c,0,0);CHECK(c.r[0]==1); /* warm hit */
    bus_write(b,0,1,2);step_at(&c,0,0);CHECK(c.r[0]==2); /* byte patch */
    bus_write(b,0x1000,2,0x0003);step_at(&c,0,0);CHECK(c.r[0]==3); /* alias patch */
    {uint32_t op=0xe3a00004;memcpy(ram,&op,4);}step_at(&c,0,0);CHECK(c.r[0]==4); /* host memcpy */
    bus_write(b,0x100,2,0x2208);step_at(&c,0x100,1);CHECK(c.r[2]==8);
    bus_write(b,0x20,2,0x8001);c.r[0]=0x100;c.r[1]=0x2209;
    step_at(&c,0x20,1);step_at(&c,0x100,1);CHECK(c.r[2]==9); /* guest STRH patches code */
    bus_write(b,0x200,1,10);bus_write(b,0x4004,4,0x200);bus_write(b,0x4008,4,0x100);
    bus_write(b,0x4010,4,0x200);bus_write(b,0x4000,2,17);
    sds_dma_advance(&dma,0);sds_dma_advance(&dma,17);
    step_at(&c,0x100,1);CHECK(c.r[2]==10&&dma.transfers==1); /* DMA patches code */
    /* Same opcode reused at another address must not cache a PC-derived value. */
    bus_write(b,0x40,4,0xea000000);bus_write(b,0x80,4,0xea000000);
    step_at(&c,0x40,0);CHECK(c.r[15]==0x48);step_at(&c,0x80,0);CHECK(c.r[15]==0x88);
    bus_write(b,0x50,2,0xe000);bus_write(b,0x90,2,0xe000);
    step_at(&c,0x50,1);CHECK(c.r[15]==0x54);step_at(&c,0x90,1);CHECK(c.r[15]==0x94);
    /* Flash identify/status reads must still invoke their device callback. */
    rom[0]=42;rom[1]=0x20;step_at(&c,0x2000,1);CHECK(c.r[0]==42);
    bus_write(b,0x2000,2,0x90);step_at(&c,0x2000,1);CHECK(c.r[1]==7&&c.last_opcode==0x2107);
    bus_write(b,0x2000,2,0xff);c.r[0]=0;step_at(&c,0x2000,1);CHECK(c.r[0]==42);
    bus_write(b,0x2000,2,0x40);bus_write(b,0x2000,2,0x2028);bus_write(b,0x2000,2,0xff);
    step_at(&c,0x2000,1);CHECK(c.r[0]==40&&flash.programs==1);
    /* A device may change the fetched value without any bus write. */
    dynamic_opcode=0xe3a0300b;step_at(&c,0x3000,0);step_at(&c,0x3000,0);
    CHECK(c.r[3]==11&&fetches==2);dynamic_opcode=0xe3a0300c;step_at(&c,0x3000,0);CHECK(c.r[3]==12&&fetches==3);
    dynamic_opcode=0x12342005;step_at(&c,0x3000,1);CHECK(c.r[0]==5); /* preserve callback width semantics */
    /* Cache hits remain real bus fetches in diagnostics. */
    log=tmpfile();CHECK(log);b->access_log=log;CHECK(bus_watch_parse(b,"0,4"));
    for(i=0;i<3;++i)step_at(&c,0,0);
    rewind(log);while(fgets(line,sizeof(line),log))if(strstr(line,"R32"))++reads;
    CHECK(reads==3);b->access_log=NULL;fclose(log);
    /* All sixteen live NZCV combinations, not just initial cache-fill flags. */
    {unsigned flags,cc;
      for(cc=0;cc<14;++cc)for(flags=0;flags<16;++flags){uint64_t clocks;int take=expected_condition(cc,flags);
        c.cpsr=0x13u|(flags<<28);clocks=c.clock_cycles;bus_write(b,0x60,4,(cc<<28)|0x0a000000u);
        step_at(&c,0x60,0);CHECK(c.r[15]==(take?0x68u:0x64u)&&c.clock_cycles-clocks==(take?3u:1u));
        c.cpsr=0x13u|(flags<<28);clocks=c.clock_cycles;bus_write(b,0x60,2,0xd000u|(cc<<8));
        step_at(&c,0x60,1);CHECK(c.r[15]==(take?0x64u:0x62u)&&c.clock_cycles-clocks==(take?3u:1u));
      }
    }
    /* Pending IRQ wins before even a warm instruction fetch. */
    {uint64_t instructions=c.instructions;c.cpsr&=~0x80u;c.irq=1;
      step_at(&c,0,0);CHECK(c.r[15]==0x18&&c.instructions==instructions);c.irq=0;}
    c.r[15]=0xdead0000;CHECK(!arm7_step(&c));CHECK(b->fault&&c.halted);
    arm7_reset(&c,b,0);c.cache=cache;b->fault=0;step_at(&c,0,0);CHECK(c.r[0]==4);
    {uint8_t replacement[]={13,0x40,0xa0,0xe3};
      bus_destroy(b);CHECK(bus_map(b,0,sizeof(replacement),replacement,1,0));
      step_at(&c,0,0);CHECK(c.r[4]==13); /* same PC after a mapping replacement */
    }
    arm7_cache_destroy(cache);bus_destroy(b);free(b);
    printf("CPU decode cache: %u checks passed.\n",checks);return 0;
}
