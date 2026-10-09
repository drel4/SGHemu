#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"SIM line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned exchange(SimCard *c,const uint8_t *command,unsigned n,uint8_t *out)
{
    unsigned i,count=0;for(i=0;i<n;++i)sim_card_send(c,command[i]);
    while(sim_card_receive(c,out+count)) {++count;CHECK(count<300);}CHECK(!c->overflow);return count;
}
static void select_file(SimCard *c,unsigned id)
{
    uint8_t cmd[]={0xa0,0xa4,0,0,2,0,0},out[300];unsigned n;
    cmd[5]=(uint8_t)(id>>8);cmd[6]=(uint8_t)id;n=exchange(c,cmd,sizeof(cmd),out);
    CHECK(n==3&&out[0]==0xa4&&out[1]==0x9f);
}
int main(void)
{
    SimCard *c=calloc(1,sizeof(*c));Sim *s=calloc(1,sizeof(*s));Bus b={0};Intc intc={0};
    uint8_t out[300],byte,ram[32]={0};unsigned n,i;uint64_t due;
    const uint8_t get[]={0xa0,0xc0,0,0,22},read[]={0xa0,0xb0,0,0,9};
    const uint8_t wrong[]={0xa0,0xb0,0,8,9},unknown[]={0xa0,0xee,0,0,0},badclass[]={0,0xa4,0,0,2};
    CHECK(c&&s);sim_card_init(c);CHECK(sim_card_receive(c,&byte)&&byte==0x3b);CHECK(sim_card_receive(c,&byte)&&byte==0);
    CHECK(!sim_card_receive(c,&byte));select_file(c,0x7f20);
    n=exchange(c,get,sizeof(get),out);CHECK(n==25&&out[0]==0xc0&&out[24]==0);CHECK(out[14]==0x90&&out[19]==0x83);
    select_file(c,0x6f07);n=exchange(c,read,sizeof(read),out);CHECK(n==12&&out[0]==0xb0&&out[1]==8&&out[2]==9);
    n=exchange(c,wrong,sizeof(wrong),out);CHECK(n==2&&out[0]==0x94&&out[1]==2);
    n=exchange(c,unknown,sizeof(unknown),out);CHECK(n==2&&out[0]==0x6d);
    n=exchange(c,badclass,sizeof(badclass),out);CHECK(n==2&&out[0]==0x6e);
    { const uint8_t missing[]={0xa0,0xa4,0,0,2,0x6f,0xff};n=exchange(c,missing,sizeof(missing),out);CHECK(n==3&&out[1]==0x94&&out[2]==4&&c->selected==0x6f07); }
    select_file(c,0x7f10);select_file(c,0x6f3a);
    { const uint8_t write[]={0xa0,0xdc,1,4,2,'A','B'},record[]={0xa0,0xb2,1,4,2};
      n=exchange(c,write,sizeof(write),out);CHECK(n==3&&out[1]==0x90);
      n=exchange(c,record,sizeof(record),out);CHECK(n==5&&out[1]=='A'&&out[2]=='B');
      sim_card_reset(c);while(sim_card_receive(c,&byte)) {}select_file(c,0x7f10);select_file(c,0x6f3a);
      n=exchange(c,record,sizeof(record),out);CHECK(n==5&&out[1]=='A'); }
    CHECK(sim_card_load(c,"models/c100/sim/test-sim.cfg"));CHECK(!strcmp(c->imsi,"001010000000001"));
    { FILE *f=fopen("build/sim-invalid.cfg","w");CHECK(f);fputs("spn=changed\nimsi=not-digits\n",f);fclose(f);CHECK(!sim_card_load(c,"build/sim-invalid.cfg"));CHECK(!strcmp(c->spn,"SGHemu"));remove("build/sim-invalid.cfg"); }
    CHECK(intc_attach(&intc,&b,0x40100));CHECK(sim_attach(s,&b,&intc,0x40a80,0x41300,0x40520));
    CHECK(!sim_attach(s,&b,&intc,0x40a80,0x41300,0x40520));CHECK(bus_read(&b,0x40a80,4)==0x20017420);
    bus_write(&b,0x40a80,2,3);bus_write(&b,0x40a80,2,11);sim_advance(s,1000);CHECK(!s->resets&&!s->card.response_size);
    bus_write(&b,0x40a80,2,3);s->present=1;bus_write(&b,0x40a80,2,11);sim_advance(s,1000);
    CHECK(s->resets==1);CHECK(bus_read(&b,0x40a8a,2)==0x3b);b.ticks=1000;sim_advance(s,2000);CHECK(bus_read(&b,0x40a8a,2)==0);
    bus_write(&b,0x4130c,2,8);bus_write(&b,0x41300,4,10);bus_write(&b,0x4130c,2,0x14);due=s->timer_due;
    intc.pending=0;sim_advance(s,due-1);CHECK(!intc.pending);sim_advance(s,due);CHECK(intc.pending&1);
    intc.pending=0;bus_write(&b,0x4130c,2,0x14);bus_write(&b,0x4130c,2,8);sim_advance(s,due+1000);CHECK(!(intc.pending&1));
    CHECK(bus_map(&b,0x400000,sizeof(ram),ram,0,0));
    memcpy(ram,unknown,sizeof(unknown));bus_write(&b,0x40524,4,0x400000);bus_write(&b,0x40528,4,0x40a88);
    bus_write(&b,0x4052c,4,0x400004);bus_write(&b,0x40520,2,0x1017);
    for(i=1;i<=5;++i)sim_advance(s,b.ticks+(uint64_t)i*1000);
    CHECK(s->card.commands==1&&s->transmitted==5&&(intc.pending&0x400));CHECK(s->dma[5]==0x400005);
    bus_write(&b,0x40520,2,0);bus_write(&b,0x40524,4,0x40a8a);bus_write(&b,0x40528,4,0x400010);
    bus_write(&b,0x4052c,4,0x400011);bus_write(&b,0x40520,2,0x100f);
    sim_advance(s,b.ticks+10000);sim_advance(s,b.ticks+11000);CHECK(ram[16]==0x6d&&ram[17]==0);
    bus_write(&b,0x40520,2,0);bus_write(&b,0x40524,4,0xfffffffe);bus_write(&b,0x40520,2,0x1017);
    sim_advance(s,b.ticks+20000);CHECK(b.fault&&!(s->dma[0]&1));b.fault=0;
    (void)bus_read(&b,0x40a81,2);CHECK(b.fault);b.fault=0;
    (void)bus_read(&b,0x41303,2);CHECK(b.fault);
    bus_destroy(&b);free(c);free(s);printf("Virtual SIM/card/transport: %u checks passed.\n",checks);return 0;
}
