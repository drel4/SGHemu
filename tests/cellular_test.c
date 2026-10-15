#include "cellular.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int main(void)
{
    Cellular *s=calloc(1,sizeof(*s));Bus *b=calloc(1,sizeof(*b));Arm7 c,original;
    uint8_t rom[128]={0},ram[32768]={0},data[]={1,2,3};unsigned i;
    const uint16_t alloc[]={0x4a05,0x6811,0x303f,0x0980,0x0180,0x1808,0x6010,0x1c08,0x4770};
    /* A real nested frame: writes well below SP, then restores its caller. */
    const uint16_t send[]={0xb510,0xb0ff,0x9000,0x4a04,0x6010,0x6051,0xb07f,0xbc10,0xbc08,0x4718};
    if(!s||!b)return 1;cellular_init(s);CHECK(!strcmp(s->network,"Tele2"));
    memcpy(rom,alloc,sizeof(alloc));rom[25]=0x90;
    memcpy(rom+32,send,sizeof(send));rom[57]=0x98;
    rom[64]=0x70;rom[65]=0x47;rom[80]=0xfe;rom[81]=0xe7;
    CHECK(bus_map(b,0x1000,sizeof(rom),rom,1,0));CHECK(bus_map(b,0x2000,sizeof(ram),ram,0,0));
    s->ready=0x1050;s->alloc=0x1000;s->send_ui=0x1020;s->release=0x1040;s->services=0x2300;s->sr_task=0x2400;
    CHECK(!cellular_set_network(s,b,1,""));CHECK(!cellular_set_network(s,b,1,"bad\nname"));
    CHECK(cellular_set_network(s,b,1,"Tele2"));s->network_pending=0;
    bus_write(b,0x9000,4,0x2000);arm7_reset(&c,b,s->ready);c.cpsr|=ARM_T;
    memset(ram+0x5800,0xa5,0x1000); /* Parked frames below the idle SP. */
    c.instructions=300000000;c.r[3]=0x12345678;c.r[13]=0x8800;original=c;
    CHECK(cellular_queue(s,0,0x163,data,3));
    for(i=0;i<100;++i){CHECK(cellular_advance(s,&c,0,1));if(!s->count&&!s->stage)break;CHECK(arm7_step(&c));}
    CHECK(i<100&&s->delivered_events==1&&s->native_stack==0x2000&&s->name_ptr==0x7000);
    CHECK(!memcmp(ram+0x5000,"Tele2",6)&&!memcmp(ram+0x5040,data,3));
    CHECK(bus_read(b,0x9800,4)==0x163&&bus_read(b,0x9804,4)==0x7040);
    for(i=0;i<0x1000;++i)CHECK(ram[0x5800+i]==0xa5);
    CHECK(!memcmp(c.r,original.r,sizeof(c.r))&&c.cpsr==original.cpsr&&c.instructions>original.instructions);
    s->boot_ready=1;original.instructions=100000000;
    s->poll=0x1052;c=original;c.r[15]=s->poll;original=c;
    CHECK(!cellular_queue(s,CELL_CALLBACK,0x1020,data,sizeof(data)));
    CHECK(cellular_queue(s,CELL_CALLBACK,0x1020,"abcdefgh",8));
    c.cpsr&=~0x80u;c.irq=1;CHECK(cellular_advance(s,&c,0,1)&&!s->stage&&s->count==1);c=original;
    c.cpsr&=~0x40u;c.fiq=1;CHECK(cellular_advance(s,&c,0,1)&&!s->stage&&s->count==1);c=original;
    c.cpsr=(c.cpsr&~31u)|0x11;CHECK(cellular_advance(s,&c,0,1)&&!s->stage&&s->count==1);c=original;
    for(i=0;i<100;++i){CHECK(cellular_advance(s,&c,0,1));if(!s->count&&!s->stage)break;CHECK(arm7_step(&c));}
    CHECK(i<100&&s->delivered_events==2&&bus_read(b,0x9800,4)==0x64636261);
    for(i=0;i<0x1000;++i)CHECK(ram[0x5800+i]==0xa5);
    CHECK(!memcmp(c.r,original.r,sizeof(c.r))&&c.cpsr==original.cpsr);
    /* Native call offering requires a second, local-alerting indication. */
    CHECK(cellular_call(s,b,"+79990001122"));CHECK(s->count==2&&s->call_state==CELL_TO_PHONE);
    CHECK(s->events[s->head].message==0x163&&s->events[s->head].data[2]==0);
    CHECK(s->events[(s->head+1)%CELL_EVENTS].message==0x162&&s->events[(s->head+1)%CELL_EVENTS].data[2]==1);
    CHECK(!cellular_call(s,b,"+1"));CHECK(cellular_answer(s,0)&&s->call_state==CELL_IDLE);
    s->head=s->count=0;
    {const uint16_t text[]={0x48,0x438};const uint8_t timestamp[]={0x62,0x01,0x41,0x21,0,0,0};
     CHECK(cellular_sms(s,"+79990001122",text,2,timestamp));
     CHECK(s->events[0].target==s->sr_task&&s->events[0].message==0x1a0);
     CHECK(s->events[0].data[0x1b]==23&&s->events[0].data[0x1c]==4&&s->events[0].data[0x1d]==11);
     CHECK(s->events[0].data[0x1c+19]==0&&s->events[0].data[0x1c+20]==0x48&&s->events[0].data[0x1c+21]==4&&s->events[0].data[0x1c+22]==0x38);
     CHECK(s->events[0].data[0x1c+10]==8);
     for(i=0;i<4;++i){s->head=s->count=0;
         CHECK(cellular_sms_class(s,"+79990001122",text,2,timestamp,(int)i));
         CHECK(s->events[0].data[0x1c+10]==(0x18|i));
         CHECK(s->events[0].data[0x1b]==23&&s->events[0].data[0x1c+20]==0x48);
         s->head=s->count=0;CHECK(cellular_sms_class(s,"12",text,2,timestamp,(int)i));
         CHECK(s->events[0].data[0x1c+5]==(0x18|i));}
     s->head=s->count=0;
     CHECK(!cellular_sms_class(s,"+1",text,2,timestamp,4)&&!s->count);
     CHECK(!cellular_sms_class(s,"+1",text,2,timestamp,-2)&&!s->count);}
    CHECK(!cellular_queue(s,0xffffffffu,0x1000,data,3));
    CHECK(!cellular_queue(s,0xffffffffu,0x1001,ram,8));
    s->head=s->count=0;s->call_state=CELL_FROM_PHONE;s->call_id=2;
    CHECK(cellular_answer(s,1)&&s->call_state==CELL_CONNECTED);
    CHECK(cellular_set_network(s,b,0,"Tele2")&&s->call_state==CELL_IDLE&&s->count==2);
    CHECK(!b->fault);bus_destroy(b);free(b);free(s);
    printf("Virtual cellular carrier: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
