#ifndef SGH_NVRAM_H
#define SGH_NVRAM_H
#include "machine.h"
/* Snapshot load is allowed only before the first guest instruction. Files are tied to
   ROM hashes/mapping and contain only the configured flash tail. */
int nvram_load(Machine *m, const char *path);
int nvram_save(Machine *m, const char *path);
typedef struct {
    char path[1024];
    void *lock;
    uint8_t saved_hash[32];
    int active, saved, provision, stage, defaults_boot;
    Arm7 caller;
} NvramStore;
/* Frontend-owned automatic persistence. Zero initialize; close before destroying
   the machine. Corrupt or locked saves fail closed, never silently reformat. */
/* NULL selects <model config directory>/state/<firmware identity>.snv and
   imports a validated legacy state/ save if no model-local save exists. */
int nvram_store_open(NvramStore *s, Machine *m, const char *directory);
int nvram_store_import(NvramStore *s, Machine *m, const char *path);
int nvram_store_flush(NvramStore *s, Machine *m);
void nvram_store_release(NvramStore *s);
/* 1: continue, 2: provisioning committed, reset from saved storage; 0: failure.
   Executes actual guest formatter/marker writer; no synthetic flash payloads. */
int nvram_store_step(NvramStore *s, Machine *m);
#endif
