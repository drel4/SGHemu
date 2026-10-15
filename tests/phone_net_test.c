#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "phone_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int main(void)
{
    Bus *bus=calloc(1,sizeof(*bus));Arm7 cpu;Cellular *cell=calloc(1,sizeof(*cell));PhoneNetwork net={0};
    uint8_t *ram=calloc(0x4000,1);const char *url="http://sghemu.local/";unsigned i,n=6+(unsigned)strlen(url),received=0;
    if(!bus||!cell||!ram)return 1;CHECK(bus_map(bus,0x5000,0x4000,ram,0,1));
    cellular_init(cell);cell->enabled=1;net.profiling=1;net.txn_start=0x100;net.send_pdu=0x200;net.recv_pdu=0x300;
    net.bearer_state=0x400;net.post_event=0x500;net.data_arrived=0x600;net.wap_base=0x5000;net.driver_base=0x6000;
    arm7_reset(&cpu,bus,0);cpu.cpsr|=ARM_T;cpu.r[14]=0x1001;cpu.r[15]=net.txn_start;
    phone_net_set_mode(&net,2);CHECK(!net.runtime&&!net.opened[0]&&!net.opened[1]);cpu.r[15]=net.bearer_state;cpu.r[2]=0x6200;cpu.r[3]=0x6204;
    CHECK(phone_net_hle(&net,cell,&cpu));CHECK(bus_read(bus,0x6200,4)==1&&!bus_read(bus,0x6204,4));
    cpu.r[15]=net.txn_start;cpu.r[0]=0;cpu.r[1]=0x2136;CHECK(phone_net_hle(&net,cell,&cpu));CHECK(net.runtime&&net.opened[0]);
    CHECK(cpu.r[15]==0x1000&&cpu.r[0]==0&&cell->count==1);
    CHECK(bus_read(bus,0x501c,4)==1&&bus_read(bus,0x5128,4)==1&&bus_read(bus,0x6004,4)==1);
    CHECK(bus_read(bus,0x5124,4)==1);
    cpu.r[15]=net.send_pdu;cpu.r[0]=0;cpu.r[2]=0x6400;cpu.r[3]=0x6600;cpu.r[13]=0x7000;
    bus_write(bus,0x6400,1,4);bus_write(bus,0x6402,1,0x23);bus_write(bus,0x6403,1,0xf1);
    bus_write(bus,0x6404,1,10);bus_write(bus,0x6407,1,1);
    {const uint8_t prefix[]={0x0a,0,1,2,0x40,19};
     for(i=0;i<6;++i)bus_write(bus,0x6600+i,1,prefix[i]);
     for(i=0;i<strlen(url);++i)bus_write(bus,0x6606+i,1,(uint8_t)url[i]);}
    bus_write(bus,0x7000,4,n);CHECK(phone_net_hle(&net,cell,&cpu)&&cpu.r[0]==0&&net.transmitted==1);
    for(i=0;i<24;++i)bus_write(bus,0x721e + i,1,0xa5); /* Beyond the native 30-byte container. */
    for(i=0;i<200;++i){phone_net_advance(&net,cell);if(cell->count<2){Sleep(5);continue;}
        cpu.r[15]=net.recv_pdu;cpu.r[0]=0;cpu.r[2]=0x7200;cpu.r[3]=0x7500;
        bus_write(bus,0x7000,4,0x7400);bus_write(bus,0x7400,4,2048);
        CHECK(phone_net_hle(&net,cell,&cpu));if(!cpu.r[0]){received=1;break;}Sleep(5);}
    CHECK(received&&net.received==1);CHECK(bus_read(bus,0x7400,4)>16);
    CHECK(bus_read(bus,0x7500,1)==0x16&&bus_read(bus,0x7503,1)==4&&bus_read(bus,0x7504,1)==0x20);
    CHECK(bus_read(bus,0x7202,1)==0x23&&bus_read(bus,0x7203,1)==0xf1);
    for(i=8;i<30;++i)CHECK(bus_read(bus,0x7200+i,1)==0);
    for(i=0;i<24;++i)CHECK(bus_read(bus,0x721e + i,1)==0xa5);
    CHECK(cell->count==2&&cell->events[1].message==net.data_arrived);
    CHECK(cell->events[1].data[4]==0x36&&cell->events[1].data[5]==0x21);
    /* An acknowledged head must not produce repeated notifications; a later
       reply must rearm delivery after the queue has drained. */
    for(i=0;i<10;++i)phone_net_advance(&net,cell);CHECK(cell->count==2);
    cpu.r[15]=net.send_pdu;cpu.r[0]=0;cpu.r[1]=0x2136;cpu.r[2]=0x6400;cpu.r[3]=0x6600;
    bus_write(bus,0x6602,1,2);bus_write(bus,0x7000,4,n);
    CHECK(phone_net_hle(&net,cell,&cpu)&&cpu.r[0]==0);
    for(i=0;i<200&&cell->count<3;++i){phone_net_advance(&net,cell);Sleep(5);}
    CHECK(cell->count==3);
    cpu.r[15]=net.recv_pdu;cpu.r[0]=0;cpu.r[2]=0x7200;cpu.r[3]=0x7500;
    bus_write(bus,0x7000,4,0x7400);bus_write(bus,0x7400,4,2048);
    CHECK(phone_net_hle(&net,cell,&cpu)&&!cpu.r[0]&&net.received==2);
    CHECK(net.lock_calls>=6);
    phone_net_set_mode(&net,0);CHECK(!net.runtime&&!net.opened[0]);
    cpu.r[15]=net.send_pdu;cpu.r[0]=0;CHECK(phone_net_hle(&net,cell,&cpu)&&cpu.r[0]==0xffffffffu);
    CHECK(!bus->fault);phone_net_close(&net);bus_destroy(bus);free(bus);free(cell);
    printf("Native WAP bridge: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
