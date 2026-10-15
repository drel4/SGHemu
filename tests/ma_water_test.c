#include "ma_water.h"
#include "emu_speed.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Water/speed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    MaWater a={0},b={0};EmuSpeed speed={0};
    static int16_t left[48000],right[48000],split_left[48000],split_right[48000];
    unsigned i,changed=0;
    for(i=0;i<48000;++i){left[i]=split_left[i]=(int16_t)(i%60001u-30000);
        right[i]=split_right[i]=(int16_t)(30000-i%60001u);}
    ma_water_pcm(&a,left,right,48000);
    CHECK(!memcmp(left,split_left,sizeof(left))&&!memcmp(right,split_right,sizeof(right)));
    ma_water_set(&a,1);ma_water_set(&b,1);ma_water_pcm(&a,left,right,48000);
    for(i=0;i<48000;i+=96)ma_water_pcm(&b,split_left+i,split_right+i,96);
    CHECK(!memcmp(left,split_left,sizeof(left))&&!memcmp(right,split_right,sizeof(right)));
    for(i=0;i<48000;++i)if(left[i]!=(int16_t)(i%60001u-30000))++changed;
    CHECK(changed>1000); /* audible damage, independent of DLL block boundaries */
    ma_water_set(&a,0);memcpy(split_left,left,sizeof(left));memcpy(split_right,right,sizeof(right));
    ma_water_pcm(&a,left,right,48000);
    CHECK(!memcmp(left,split_left,sizeof(left))&&!memcmp(right,split_right,sizeof(right)));
    {uint32_t rng=0;unsigned errors=0,bad=0,burst=0,longest=0,run=0;
        for(i=0;i<65536;++i){unsigned value=i&255u;
            unsigned delta=ma_water_sample_byte(&rng,&burst,value)^value;
            if(delta){++run;if(run>longest)longest=run;}else run=0;
            if(delta){++errors;if(delta!=1&&delta!=2&&delta!=4&&delta!=8)++bad;}}
        CHECK(errors>250&&errors<1500);CHECK(!bad);CHECK(longest>=12&&longest<=45);
    }
    emu_speed_sample(&speed,0,1000,0,1);CHECK(!speed.valid);
    emu_speed_sample(&speed,500,1000,500000000,1);CHECK(speed.valid&&speed.percent==100);
    emu_speed_sample(&speed,1000,1000,1500000000,1);CHECK(speed.percent==200);
    emu_speed_sample(&speed,1500,1000,1750000000,1);CHECK(speed.percent==50);
    emu_speed_sample(&speed,2000,1000,1750000000,1);CHECK(speed.percent==0); /* no progress */
    emu_speed_sample(&speed,2100,1000,1750000000,0);CHECK(!speed.valid&&!speed.primed&&speed.percent==0);
    emu_speed_sample(&speed,6000,1000,1750000000,1);
    emu_speed_sample(&speed,6500,1000,2250000000,1);CHECK(speed.percent==100); /* pause excluded */
    emu_speed_sample(&speed,7000,1000,0,1);CHECK(!speed.valid); /* reset */
    emu_speed_sample(&speed,7500,1000,500000000,1);CHECK(speed.percent==100);
    emu_speed_sample(&speed,7600,0,500000000,1);CHECK(!speed.valid);
    printf("Water PCM and speed meter: %u checks passed\n",checks);return 0;
}
