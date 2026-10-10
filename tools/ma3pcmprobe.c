#include "ma3.h"
#include <stdio.h>
/* Exercise the guest-facing ports, including literal bytes with bit 7 set. */
static Bus bus;
static void ss(unsigned value){bus_write(&bus,0x200000,1,value);}
static void data(unsigned value){bus_write(&bus,0x200004,1,value);}
static void parameter(unsigned address,unsigned value)
{
    ss(2);
    if(address>=128){data(address&127);data((address>>7)|128);}
    else data(address|128);
    data(value|128);
}
static void ram(unsigned address,const unsigned char *bytes,unsigned size)
{
    unsigned i;ss(2);data(address&127);data((address>>7)&127);data((address>>14)|128);
    data(size&127);data((size>>7)|128);
    for(i=0;i<size;++i)data(bytes[i]);
}
static int sample(Ma3 *m,unsigned format)
{
    unsigned char descriptor[13]={2,0,0,0xf0,0xf0,0,0,0x40,0x20,0,0,1,0},bytes[512];
    unsigned i;Ma5HwStats before,after;
    ma5_hw_reset(m->hw);descriptor[1]=(unsigned char)format;
    for(i=0;i<sizeof(bytes);++i)bytes[i]=(unsigned char)(format?((i/8)&1?0x30:0xd0):0x77);
    ram(0x4000,descriptor,sizeof(descriptor));ram(0x4020,bytes,sizeof(bytes));
    ss(4);data(0);ss(7);data(31);
    parameter(0x90,0x7c);parameter(0xa0,0x3c);
    parameter(0x60,0x40);parameter(0x61,0);parameter(0x62,0);
    parameter(0x63,2);parameter(0x64,0);parameter(0x65,0x40);
    before=ma5_hw_stats(m->hw);
    for(i=0;i<20;++i)if(!ma5_hw_generate(m->hw,480))return 0;
    after=ma5_hw_stats(m->hw);
    { unsigned cursor=ma5_hw_pcm_cursor(m->hw,0);
      printf("Native cursor: format=%u samples=%u\n",cursor>>24,cursor&0xffff);
      if(cursor==UINT32_MAX||(cursor>>24)!=format||(cursor&0xffff)>256)return 0;
    }
    if(ma5_hw_pcm_cursor(m->hw,32)!=UINT32_MAX)return 0;
    printf("MA-3 port sample format %u: nonzero=%llu peak=%u\n",format,
        (unsigned long long)(after.nonzero-before.nonzero),after.peak);
    return after.nonzero>before.nonzero&&!after.failed&&!bus.fault&&!*m->error;
}
static int stream_event(Ma3 *m,unsigned channel)
{
    unsigned i,result=0,payload;
    ma5_hw_reset(m->hw);
    parameter(0x162,0x7f);parameter(0x15e,6);parameter(0x15f,97);
    ss(1);data(0x8a);data(0x5a+channel);data(0x82);data(0xc3+channel);
    parameter(0x161,3);
    for(i=1;i<=20;++i) {
        if(!ma5_hw_generate(m->hw,48))return 0;
        ss(0);result=bus_read(&bus,0x200004,1);
        if(result&(1u<<channel))break;
    }
    ss(3);data(0x5a+channel);data(0x82);data(0x80);ss(0x11);
    payload=bus_read(&bus,0x200004,1);
    printf("MA-3 stream %u: IRQ=%02x at %u ms payload=%02x\n",channel,result,i,payload);
    return i>=10&&i<=13&&(result&(1u<<channel))&&payload==0x43+channel;
}
static int refill_timer(Ma3 *m)
{
    unsigned i,irq=0;
    ma5_hw_reset(m->hw);parameter(0x162,0x7f);
    parameter(0x152,27);parameter(0x154,20);parameter(0x155,1);
    for(i=1;i<=30;++i) {
        if(!ma5_hw_generate(m->hw,48))return 0;
        ss(0);irq=bus_read(&bus,0x200004,1);if(irq&0x20)break;
    }
    printf("MA-3 refill timer: IRQ=%02x at %u ms\n",irq,i);
    ss(0);data(0x20);ss(0);
    if(bus_read(&bus,0x200004,1)&0x20)return 0;
    parameter(0x155,0);
    return i>=20&&i<=23&&(irq&0x20);
}
static void timed_payload(unsigned delay,unsigned value)
{
    ss(1);
    do {unsigned byte=delay&127;delay>>=7;data(byte|(delay?0:128));}while(delay);
    data(0x5a);data(0x82);data(value|128);
}
static int interrupted_sequence(Ma3 *m)
{
    unsigned i,irq=0,payload;
    ma5_hw_reset(m->hw);ss(4);data(0);
    parameter(0x162,0x7f);parameter(0x15e,6);parameter(0x15f,97);
    timed_payload(100,1);timed_payload(1000,2);parameter(0x161,3);
    for(i=0;i<50;++i)if(!ma5_hw_generate(m->hw,480))return 0;
    parameter(0x161,0);ss(0);data(0xff);
    ss(4);data(0x20);data(0);
    timed_payload(100,3);
    /* C100 refill recovery: clear, refill, sequence-reset pulse, start. */
    ss(4);data(8);data(0);parameter(0x161,1);
    for(i=1;i<=200;++i) {
        if(!ma5_hw_generate(m->hw,48))return 0;
        ss(0);irq=bus_read(&bus,0x200004,1);if(irq&1)break;
    }
    ss(3);data(0x5a);data(0x82);data(0x80);ss(0x11);
    payload=bus_read(&bus,0x200004,1);
    printf("MA-3 interrupted sequence: next 100-tick event at %u ms payload=%02x\n",i,payload);
    return i>=100&&i<=103&&payload==3;
}
int main(int argc,char **argv)
{
    Ma3 m={0};Intc intc={0};unsigned i;int ok=1;
    if(argc!=2){fprintf(stderr,"Usage: ma3pcmprobe M5_EmuHw.dll\n");return 2;}
    intc.attached=1;
    if(!ma3_attach(&m,&bus,&intc,0x200000,0x800000,1157)||
       !ma3_enable(&m,argv[1],"build/ma3-pcm-probe.wav",0,1000000000)) {
        fprintf(stderr,"%s\n",m.error);ma3_destroy(&m);bus_destroy(&bus);return 1;
    }
    for(i=0;i<3;++i)if(!sample(&m,i))ok=0;
    for(i=0;i<2;++i)if(!stream_event(&m,i))ok=0;
    if(!refill_timer(&m))ok=0;
    if(!interrupted_sequence(&m))ok=0;
    ma3_destroy(&m);bus_destroy(&bus);return ok?0:1;
}
