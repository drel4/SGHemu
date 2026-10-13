#include "phone_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned checks;
static uint32_t device_read(void *ctx,uint32_t offset,unsigned width)
{(void)ctx;(void)offset;(void)width;return 0;}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"phone IO line %d\n",__LINE__);exit(1);}}while(0)
int main(void){Bus bus={0};Arm7 cpu={0};PhoneIo io={0};uint8_t ram[1024]={0};uint64_t before;
    static uint8_t rom[]={0,0,0,0,0,0,0xe8,3};uint32_t ignored;
    CHECK(bus_map(&bus,0x1000,sizeof(ram),ram,0,0));arm7_reset(&cpu,&bus,0);cpu.cpsr|=ARM_T;
    io.configured=1;io.tone_entry=0x2000;io.tone_stop=0x2010;io.tone_table=0x1100;io.gpio_entry=0x2020;
    io.led_pins[0]=5;io.led_pins[1]=3;io.led_pins[2]=6;
    bus_write(&bus,0x1000,1,1);bus_write(&bus,0x1001,1,0);
    bus_write(&bus,0x1104,2,0);bus_write(&bus,0x1106,2,1000); /* cos(pi/2): 2000 Hz */
    cpu.r[0]=0x1000;cpu.r[1]=0x1001;cpu.r[15]=io.tone_entry;
    phone_io_observe(&io,&cpu);CHECK(io.tone_changes==1&&io.amplitude[0]==4000&&io.amplitude[1]==0);
    CHECK(fabs(io.step[0]*48000/(2*3.14159265358979323846)-2000)<0.01);
    CHECK(cpu.r[15]==io.tone_entry&&cpu.r[0]==0x1000); /* Observer, not HLE. */
    /* Real model tables live in flash: a read callback remains installed even
       when fast=array mode. Do not accidentally treat them as unreadable MMIO. */
    CHECK(bus_map(&bus,0x3000,sizeof(rom),rom,1,0));
    bus.regions[1].read=device_read;bus.regions[1].fast=1;io.tone_table=0x3000;
    CHECK(!bus_peek(&bus,0x3004,2,&ignored));io.amplitude[0]=0;
    phone_io_observe(&io,&cpu);CHECK(io.amplitude[0]==4000);
    CHECK(phone_io_open(&io,0,"build/cpu-tone-test.wav"));phone_io_advance(&io,100000000);
    CHECK(io.frames==4800&&io.nonzero>4000);before=io.nonzero;
    cpu.r[15]=io.tone_stop;phone_io_observe(&io,&cpu);phone_io_advance(&io,200000000);
    CHECK(io.amplitude[0]==0&&io.amplitude[1]==0&&io.nonzero-before<1000);
    before=io.nonzero;phone_io_advance(&io,300000000);CHECK(io.nonzero==before);
    cpu.r[15]=io.gpio_entry;cpu.r[0]=5;cpu.r[1]=1;phone_io_observe(&io,&cpu);CHECK(io.led==1);
    cpu.r[0]=3;phone_io_observe(&io,&cpu);CHECK(io.led==3);
    cpu.r[0]=6;phone_io_observe(&io,&cpu);CHECK(io.led==7);
    cpu.r[1]=0;phone_io_observe(&io,&cpu);CHECK(io.led==3);
    cpu.r[0]=9;cpu.r[1]=1;phone_io_observe(&io,&cpu);CHECK(io.led==3&&io.led_changes==4);
    {int16_t left[48],right[48];unsigned i;uint64_t frame=io.frames;
        for(i=0;i<48;++i){left[i]=123;right[i]=-123;}
        io.mixed=1;phone_io_advance(&io,400000000);CHECK(io.frames==frame);
        phone_io_mix(&io,left,right,48);CHECK(io.frames==frame+48);
        for(i=0;i<48;++i)CHECK(left[i]==123&&right[i]==-123);
        io.amplitude[0]=io.render_amplitude[0]=20000;io.phase[0]=3.14159265358979323846/2;io.step[0]=0;
        left[0]=32000;right[0]=-32000;phone_io_mix(&io,left,right,1);
        CHECK(left[0]==32767&&right[0]==-12000);
    }
    phone_io_close(&io);CHECK(!io.output);bus_destroy(&bus);
    printf("CPU tone/LED output: %u checks passed.\n",checks);return 0;
}
