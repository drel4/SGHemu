#ifndef SGH_EMU_SPEED_H
#define SGH_EMU_SPEED_H
#include <stdint.h>
typedef struct {uint64_t wall,guest;double percent;int primed,valid;} EmuSpeed;
/* Half-second windows include host waits and audio pacing. No CPU-MHz guess. */
static inline void emu_speed_sample(EmuSpeed *s,uint64_t wall,uint64_t frequency,uint64_t guest,int running)
{
    if(!running||!frequency){s->primed=s->valid=0;s->percent=0;return;}
    if(!s->primed||wall<s->wall||guest<s->guest){s->wall=wall;s->guest=guest;
        s->primed=1;s->valid=0;s->percent=0;return;}
    if(wall-s->wall>=frequency/2&&wall!=s->wall){
        s->percent=100.0*((double)(guest-s->guest)/1000000000.0)*
            (double)frequency/(double)(wall-s->wall);
        s->wall=wall;s->guest=guest;s->valid=1;
    }
}
#endif
