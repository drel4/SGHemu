#ifndef SGH_IROM_H
#define SGH_IROM_H
#include "arm7.h"
#define IROM_MAX_SERVICES 16
enum { IROM_MEMCPY = 1, IROM_VERSION };
typedef struct { uint32_t address; unsigned kind; uint64_t calls; } IromService;
typedef struct {
    IromService services[IROM_MAX_SERVICES];
    unsigned count;
    uint32_t first_address,last_address;
    uint16_t version;
    uint32_t version_return;
    int version_configured;
} Irom;
int irom_add(Irom *rom, uint32_t address, const char *name);
/* 0: not handled; 1: completed; -1: invalid call, CPU halted.
   The caller must deliver pending CPU exceptions before invoking this hook. */
int irom_step(Irom *rom, Arm7 *cpu);
static inline int irom_may_call(const Irom *rom,uint32_t pc)
{return rom->count&&pc>=rom->first_address&&pc<=rom->last_address;}
#endif
