#ifndef SGHEMU_MA_DETUNE_H
#define SGHEMU_MA_DETUNE_H
#include <stdint.h>
#include <string.h>
/* Optional C100 clock/pitch approximation: midpoint of the reported
   10-15 cents flat range. Resampling also lengthens MA playback by ~0.72%.
   CPU tones are mixed AFTER this conversion and remain unaffected. */
#define MA_DETUNE_STEP 4264068101ULL
#define MA_DETUNE_ONE 4294967296ULL
typedef struct {uint64_t phase;int16_t left,right;int enabled,primed;} MaDetune;
static inline void ma_detune_set(MaDetune *d,int enabled)
{
    enabled=!!enabled;if(d->enabled!=enabled){d->enabled=enabled;d->primed=0;d->phase=0;}
}
static inline unsigned ma_detune_pcm(MaDetune *d,const int16_t *left,const int16_t *right,unsigned frames,
    int16_t *out_left,int16_t *out_right,unsigned capacity)
{
    unsigned i,n=0;if(!d->enabled){if(frames>capacity)return 0;
        memcpy(out_left,left,frames*sizeof(*left));memcpy(out_right,right,frames*sizeof(*right));return frames;}
    for(i=0;i<frames;++i){
        if(!d->primed){if(n==capacity)return 0;out_left[n]=left[i];out_right[n++]=right[i];
            d->primed=1;d->phase=MA_DETUNE_STEP;
        }else {
            while(d->phase<=MA_DETUNE_ONE){
                int64_t l=(int64_t)d->left+((int64_t)(left[i]-d->left)*(int64_t)d->phase)/(int64_t)MA_DETUNE_ONE;
                int64_t r=(int64_t)d->right+((int64_t)(right[i]-d->right)*(int64_t)d->phase)/(int64_t)MA_DETUNE_ONE;
                if(n==capacity)return 0;out_left[n]=(int16_t)l;out_right[n++]=(int16_t)r;d->phase+=MA_DETUNE_STEP;
            }
            d->phase-=MA_DETUNE_ONE;
        }
        d->left=left[i];d->right=right[i];
    }return n;
}
#endif
