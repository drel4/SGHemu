#include "ma5_hw.h"
#include <stdio.h>
/* Native sequencer restart experiment; no firmware or sample assets needed. */
static unsigned status(Ma5Hw *h)
{
    ma5_hw_write(h,0,0);return ma5_hw_read(h,1);
}
static void packet(Ma5Hw *h)
{
    ma5_hw_write(h,0,1);ma5_hw_write(h,1,0xe4); /* 100 ticks */
    ma5_hw_write(h,1,0x56);ma5_hw_write(h,1,0x83); /* IRQ payload 1D6 */
    ma5_hw_write(h,1,0xc1);
}
static unsigned run(Ma5Hw *h,unsigned start)
{
    unsigned i;
    packet(h);ma5_hw_register(h,0x26,start,0);
    for(i=1;i<=200;++i) {
        if(!ma5_hw_generate(h,48))return 0;
        if(status(h)&4)return i;
    }
    printf("No IRQ after start=%u: payload=%02x\n",start,ma5_hw_memory(h,0x1d6,0));
    ma5_hw_diagnostics(h);
    return 0;
}
int main(int argc,char **argv)
{
    Ma5Hw *h;char error[256]={0};unsigned first,restart,reset,i;
    if(argc!=2){fprintf(stderr,"Usage: ma5seqprobe M5_EmuHw.dll\n");return 2;}
    h=ma5_hw_open(argv[1],NULL,0,error,sizeof(error));
    if(!h){fprintf(stderr,"%s\n",error);return 1;}
    ma5_hw_register(h,0x11,0xff,0);
    /* 922 units * 1085 ns = 1.00037 ms per tick. */
    ma5_hw_register(h,0x23,922>>6,0);ma5_hw_register(h,0x24,(922&63)<<2,0);
    first=run(h,3);for(i=0;i<50;++i)ma5_hw_generate(h,480);
    ma5_hw_register(h,0x26,0,0);ma5_hw_register(h,0,0xff,0);
    ma5_hw_register(h,4,0x60,0);ma5_hw_register(h,4,0,0);
    restart=run(h,1);
    ma5_hw_register(h,0x26,0,0);ma5_hw_register(h,0,0xff,0);
    ma5_hw_register(h,4,0x60,0);ma5_hw_register(h,4,0,0);
    reset=run(h,3);
    printf("100-tick IRQ: first=%u ms, FIFO-clear/start=%u ms, counter-reset/start=%u ms\n",
        first,restart,reset);
    ma5_hw_close(h);
    /* A FIFO clear reanchors the delay parser even without resetting the
       free-running native counter. Do not reset time on every resume. */
    return first>=99&&first<=103&&restart>=99&&restart<=103&&reset>=99&&reset<=103?0:1;
}
