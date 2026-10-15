#ifndef SGH_EMU_SPEED_H
#define SGH_EMU_SPEED_H
#include <stdint.h>
typedef struct {
    uint64_t wall[9],guest[9],observed_wall,observed_guest;
    unsigned head,count;double percent;int primed,valid;
} EmuSpeed;
/* Quarter-second samples, rolling two-second actual-progress average. Call
   only with a fresh guest/wall pair, never a cached guest timestamp. */
static inline void emu_speed_sample(EmuSpeed *s,uint64_t wall,uint64_t frequency,uint64_t guest,int running)
{
    unsigned last,slot;
    if(!running||!frequency){s->primed=s->valid=0;s->count=0;s->percent=0;return;}
    if(!s->primed||wall<s->observed_wall||guest<s->observed_guest){
        s->head=0;s->count=1;s->wall[0]=wall;s->guest[0]=guest;
        s->observed_wall=wall;s->observed_guest=guest;s->primed=1;s->valid=0;s->percent=0;return;}
    s->observed_wall=wall;s->observed_guest=guest;
    last=(s->head+s->count-1)%9;
    if(wall==s->wall[last]||wall-s->wall[last]<frequency/4)return;
    if(s->count==9){s->head=(s->head+1)%9;--s->count;}
    slot=(s->head+s->count++)%9;s->wall[slot]=wall;s->guest[slot]=guest;
    while(s->count>2&&wall-s->wall[(s->head+1)%9]>=frequency*2){s->head=(s->head+1)%9;--s->count;}
    if(wall-s->wall[s->head]>=frequency/2){
        s->percent=100.0*((double)(guest-s->guest[s->head])/1000000000.0)*
            (double)frequency/(double)(wall-s->wall[s->head]);s->valid=1;
    }
}
/* An unavailable snapshot is unknown, not zero progress. Keep brief missed
   refreshes out of the measurement; do not display an old speed indefinitely. */
static inline void emu_speed_stale(EmuSpeed *s,uint64_t wall,uint64_t frequency)
{if(!frequency||wall<s->observed_wall||wall-s->observed_wall>=frequency*2)s->valid=0;}
#endif
