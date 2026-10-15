#ifndef SGH_AUDIO_H
#define SGH_AUDIO_H
#include <stdint.h>
#include <stddef.h>
/* Host-owned SMAF data. Backend owns a copy until its complete teardown.
   The silent backend intentionally reports no fabricated playback-finished IRQ.
   Device registers/FIFO are added only after tracing the firmware driver. */
typedef struct Audio Audio;
typedef struct {
    int (*play)(Audio *, const uint8_t *, size_t, unsigned);
    int (*stop)(Audio *);
    int (*volume)(Audio *, unsigned);
    void (*advance)(Audio *, uint64_t); /* optional: NULL when no timed work exists */
} AudioOps;
struct Audio { const AudioOps *ops; void *state; };
static inline int audio_silent_play(Audio *a, const uint8_t *p, size_t n, unsigned loops)
{ (void)a; (void)p; (void)n; (void)loops; return 1; }
static inline int audio_silent_stop(Audio *a) { (void)a; return 1; }
static inline int audio_silent_volume(Audio *a, unsigned volume) { (void)a; return volume <= 127; }
static inline Audio audio_silent(void)
{
    static const AudioOps ops = { audio_silent_play, audio_silent_stop, audio_silent_volume, NULL };
    Audio a = { &ops, NULL }; return a;
}
#endif
