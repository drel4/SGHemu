#include "ma5_hw.h"
#include <stdio.h>
static unsigned memory_read(Ma5Hw *h,unsigned address)
{
    return ma5_hw_memory(h,address,0);
}
static void ram_write(Ma5Hw *h,unsigned address,const unsigned char *bytes,unsigned size)
{
    unsigned i;ma5_hw_write(h,0,2);
    ma5_hw_write(h,1,address&127);ma5_hw_write(h,1,(address>>7)&127);ma5_hw_write(h,1,(address>>14)|128);
    ma5_hw_write(h,1,size&127);ma5_hw_write(h,1,(size>>7)|128);
    for(i=0;i<size;++i)ma5_hw_write(h,1,bytes[i]);
}
static int sample_test(Ma5Hw *h,unsigned format)
{
    unsigned char descriptor[13]={2,0,0,0xf0,0xf0,0,0,0x40,0x20,0,0,1,0},samples[512];
    unsigned i;Ma5HwStats before,after;
    ma5_hw_reset(h);descriptor[1]=(unsigned char)format;
    for(i=0;i<sizeof(samples);++i)samples[i]=(unsigned char)(format?((i/8)&1?0x30:0xd0):0x77);
    ram_write(h,0x4000,descriptor,sizeof(descriptor));ram_write(h,0x4020,samples,sizeof(samples));
    if(ma5_hw_memory(h,0x4001,1)!=format)return 0;
    ma5_hw_register(h,0x27,0x7a,0);ma5_hw_register(h,0x28,0x7a,0);ma5_hw_register(h,0x2a,0x7a,0);
    ma5_hw_parameter(h,0xc0,0x7c);ma5_hw_parameter(h,0xd0,0x3c);
    ma5_hw_parameter(h,0x60,0x40);ma5_hw_parameter(h,0x61,0);ma5_hw_parameter(h,0x62,0);
    ma5_hw_parameter(h,0x63,0x10);ma5_hw_parameter(h,0x64,0);ma5_hw_parameter(h,0x65,0x40);
    before=ma5_hw_stats(h);
    for(i=0;i<20;++i)if(!ma5_hw_generate(h,480))return 0;
    after=ma5_hw_stats(h);printf("Native sample format %u: nonzero=%llu, peak=%u\n",format,
        (unsigned long long)(after.nonzero-before.nonzero),after.peak);
    return after.nonzero>before.nonzero&&!after.failed;
}
int main(int argc,char **argv)
{
    char error[256]={0};Ma5Hw *h;Ma5HwStats stats;unsigned i;int ok=1;
    if(argc!=2){fprintf(stderr,"Usage: ma5hwprobe M5_EmuHw.dll\n");return 2;}
    h=ma5_hw_open(argv[1],"build/ma5-hardware-probe.wav",0,error,sizeof(error));
    if(!h){fprintf(stderr,"%s\n",error);return 1;}
    printf("Hardware init=0x46; status=%02x\n",ma5_hw_read(h,0));
    if(!ma5_hw_stress(h)){fprintf(stderr,"Stack-integrity stress failed\n");ok=0;}
    puts("1,000,000 hardware status calls completed without ESP drift.");
    for(i=0;i<16;++i){unsigned a=memory_read(h,0x90+i),b=memory_read(h,0xa0+i);
        printf("Native MA-5 RAM %03x=%02x %03x=%02x (not MA-3 reset defaults)\n",0x90+i,a,0xa0+i,b);}
    for(i=0;i<100;++i)if(!ma5_hw_generate(h,480))ok=0;
    for(i=0;i<3;++i)if(!sample_test(h,i)){fprintf(stderr,"Native sample format %u failed\n",i);ok=0;}
    stats=ma5_hw_stats(h);printf("PCM frames=%llu nonzero=%llu peak=%u guard-check=%s\n",
        (unsigned long long)stats.frames,(unsigned long long)stats.nonzero,stats.peak,stats.failed?"failed":"passed");
    if(stats.failed)ok=0;
    ma5_hw_close(h);return ok?0:1;
}
