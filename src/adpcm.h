#ifndef SGH_ADPCM_H
#define SGH_ADPCM_H
#include <stdint.h>
/* Yamaha nibble step used only for the refilled ring's interpolation lookahead.
   Decoder history is committed once, after the native cursor actually wraps. */
static inline int32_t sgh_adpcm_next(unsigned nibble,uint32_t *step,int32_t predictor)
{
    static const unsigned scale[8]={230,230,230,230,307,409,512,614};
    unsigned magnitude=nibble&7;uint32_t old=*step;
    int32_t delta=(int32_t)((old>>3)+((magnitude&1)?old>>2:0)+
        ((magnitude&2)?old>>1:0)+((magnitude&4)?old:0));
    int32_t sample=predictor+((nibble&8)?-delta:delta);
    uint32_t next=(old*scale[magnitude])>>8;
    *step=next<127?127:next>24576?24576:next;
    return sample< -32768?-32768:sample>32767?32767:sample;
}
#endif
