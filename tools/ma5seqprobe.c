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
    ma5_hw_pump(h);
    if(status(h)&4){fprintf(stderr,"Zero-time pump executed a future event early\n");return 0;}
    for(i=1;i<=200;++i) {
        if(!ma5_hw_generate(h,48))return 0;
        if(status(h)&4)return i;
    }
    printf("No IRQ after start=%u: payload=%02x\n",start,ma5_hw_memory(h,0x1d6,0));
    ma5_hw_diagnostics(h);
    return 0;
}
static int led_test(Ma5Hw *h)
{
    unsigned i;const uint8_t on[]={0xe4,0x5e,0x83,0x83};
    const uint8_t off[]={0x8a,0x5e,0x83,0x84};
    ma5_hw_fifo(h,1,on,sizeof(on));ma5_hw_register(h,0x26,3,0);ma5_hw_pump(h);
    if(ma5_hw_led_token(h))return 0;
    for(i=1;i<=104;++i){if(!ma5_hw_generate(h,48))return 0;
        if(i<99&&ma5_hw_led_token(h))return 0;}
    if(ma5_hw_led_token(h)!=3)return 0;
    ma5_hw_fifo(h,1,off,sizeof(off));ma5_hw_pump(h);
    if(ma5_hw_led_token(h)!=3)return 0;
    for(i=1;i<=14;++i){if(!ma5_hw_generate(h,48))return 0;
        if(i<9&&ma5_hw_led_token(h)!=3)return 0;}
    if(ma5_hw_led_token(h)!=4)return 0;
    puts("LED bridge: on after 100 ticks, off after 10 ticks; no early FIFO-receipt toggle");
    return 1;
}
int main(int argc,char **argv)
{
    Ma5Hw *h;char error[256]={0};unsigned first,restart,reset,i;
    if(argc!=2){fprintf(stderr,"Usage: ma5seqprobe M5_EmuHw.dll\n");return 2;}
    h=ma5_hw_open(argv[1],NULL,0,error,sizeof(error));
    if(!h){fprintf(stderr,"%s\n",error);return 1;}
    {
        const uint8_t setup[]={0x80,0x40,0x81,0xe0}; /* delta=0, C0 volume=60 */
        ma5_hw_parameter(h,0xc0,0x80);
        ma5_hw_fifo(h,1,setup,sizeof(setup));
        ma5_hw_register(h,0x26,1,0);
        ma5_hw_pump(h);
        if(!(ma5_hw_read(h,0)&2)||ma5_hw_memory(h,0xc0,0)!=0x60||ma5_hw_stats(h).frames) {
            fprintf(stderr,"Zero-time startup FIFO pump failed\n");ma5_hw_close(h);return 1;
        }
        ma5_hw_register(h,0x26,0,0);
        ma5_hw_register(h,4,0x28,0);ma5_hw_register(h,4,0,0);
        printf("Zero-time startup FIFO: drained, volume restored, no PCM frames\n");
    }
    ma5_hw_register(h,0x11,0xff,0);
    /* 922 units * 1085 ns = 1.00037 ms per tick. */
    ma5_hw_register(h,0x23,922>>6,0);ma5_hw_register(h,0x24,(922&63)<<2,0);
    /* Drain in 48-frame steps so the staging fill stays a multiple of the
       step (480/48 = 10); generate(480) after a 48-step run would overflow the
       staging buffer and latch a permanent backend failure. */
    if(!led_test(h)){fprintf(stderr,"Sequenced LED bridge failed\n");ma5_hw_close(h);return 1;}
    first=run(h,3);for(i=0;i<500;++i)ma5_hw_generate(h,48);
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
