#ifndef SGH_MA_WATER_H
#define SGH_MA_WATER_H
#include <stdint.h>
#include <string.h>
/* Deliberately fictional shorted-pin effect, applied to MA PCM before CPU
   tones are mixed. Never sends malformed packets/pointers to the native DLL. */
typedef struct {uint32_t rng;unsigned remaining,mode,hold,period;int enabled;int16_t left,right;} MaWater;
static inline uint32_t ma_water_random(uint32_t *state)
{
    uint32_t x=*state?*state:0xa511e9b3u;
    x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
static inline void ma_water_set(MaWater *w,int enabled)
{
    if(w->enabled!=!!enabled){memset(w,0,sizeof(*w));w->enabled=!!enabled;}
}
/* Sample upload data only, never packet headers or length/address bytes.
   Sparse low-nibble bit errors perturb PCM/ADPCM without changing transport. */
static inline unsigned ma_water_sample_byte(uint32_t *rng,unsigned *burst,unsigned value)
{
    uint32_t r=ma_water_random(rng);
    if(!*burst&&(r&4095u)==0)*burst=12+((r>>12)&31u);
    if(*burst){--*burst;return value^(1u<<((r>>9)&3u));}
    return (r&511u)==0?value^(1u<<((r>>9)&3u)):value;
}
static inline void ma_water_pcm(MaWater *w,int16_t *left,int16_t *right,unsigned frames)
{
    unsigned i;if(!w->enabled)return;
    for(i=0;i<frames;++i){uint32_t r=ma_water_random(&w->rng);int l=left[i],v=right[i];
        if(!w->remaining){w->mode=r%5;w->remaining=2400+(r>>8)%9600;
            w->period=2+(r>>20)%47;w->hold=0;}
        --w->remaining;
        if(w->mode==1){ /* pin chatter / sample-and-hold */
            if(!w->hold){w->left=(int16_t)l;w->right=(int16_t)v;w->hold=w->period;}
            --w->hold;l=w->left;v=w->right;
        }else if(w->mode==2){l=(l/1024)*1024;v=(v/1024)*1024;}
        else if(w->mode==3&&w->remaining%2400<720)l=v=0;
        else if(w->mode==4){l=l/3;v=v/3;}
        /* Sparse electrical crackles, bounded independently of source level. */
        if((r&2047u)==0){int crack=(r&4096u)?6000:-6000;l+=crack;v+=crack;}
        left[i]=(int16_t)(l>32767?32767:l< -32768?-32768:l);
        right[i]=(int16_t)(v>32767?32767:v< -32768?-32768:v);
    }
}
#endif
