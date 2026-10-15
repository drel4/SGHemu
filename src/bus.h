#ifndef SGH_BUS_H
#define SGH_BUS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BUS_MAX_REGIONS 64
#define BUS_PAGE_SHIFT 16
#define BUS_MAX_WATCH 8
typedef uint32_t (*PeripheralRead)(void *, uint32_t, unsigned);
typedef void (*PeripheralWrite)(void *, uint32_t, unsigned, uint32_t);
typedef struct {
    uint32_t base, size;
    uint8_t *data;
    int readonly, owned;
    int fast; /* read callback may be bypassed: data[] is currently exact (flash array mode) */
    PeripheralRead read;
    PeripheralWrite write;
    void *context;
} Region;
typedef struct {
    Region regions[BUS_MAX_REGIONS];
    unsigned count;
    uint32_t pc;
    uint64_t ticks, faults;
    FILE *log;
    FILE *access_log; /* Optional inclusive-start/exclusive-end watch ranges. */
    uint32_t watch_start, watch_end; /* Legacy single range; mirrors the first parsed range. */
    uint32_t watch_lo[BUS_MAX_WATCH], watch_hi[BUS_MAX_WATCH];
    unsigned watch_count; /* Armed ranges. Zero plus an empty legacy range disables the watch. */
    int watch_writes_only; /* Watch ranges: record writes only (state-transition hunting). */
    int fault;
    uint32_t last_address, last_value;
    unsigned last_width;
    int last_write;
    int pages_ready; /* page[] is rebuilt lazily after regions change */
    uint32_t map_generation;
    uint8_t page[1u << (32 - BUS_PAGE_SHIFT)]; /* region index + 1 when one region owns the whole 64 KiB page */
} Bus;
/* Diagnostics only: arm the watch from "start,end[;start,end...]". Keeps the
   legacy watch_start/watch_end in sync with the first range. Returns the number
   of ranges armed (0 on a malformed spec). */
unsigned bus_watch_parse(Bus *b, const char *spec);
int bus_map(Bus *b, uint32_t base, uint32_t size, uint8_t *data, int readonly, int owned);
int bus_device(Bus *b, uint32_t base, uint32_t size, PeripheralRead read, PeripheralWrite write, void *ctx);
uint32_t bus_read(Bus *b, uint32_t address, unsigned width);
typedef struct {Region *region;uint32_t generation;Bus *owner;} BusFetchHint;
Region *bus_fetch_region(Bus *b,uint32_t address,unsigned width,BusFetchHint *hint);
/* Always read live bytes. Never bypass device modes, fault handling or watches. */
static inline uint32_t bus_fetch(Bus *b,uint32_t address,unsigned width,BusFetchHint *hint)
{
    Region *r=hint->region;uint32_t value=0,offset;
    if(width!=2&&width!=4)return bus_read(b,address,width);
    if(!r||hint->owner!=b||hint->generation!=b->map_generation||r->size<width||address-r->base>r->size-width)
        r=bus_fetch_region(b,address,width,hint);
    if(!r||b->access_log||(r->read&&!r->fast))return bus_read(b,address,width);
    offset=address-r->base;
    if(width==4)memcpy(&value,r->data+offset,4);
    else {uint16_t half;memcpy(&half,r->data+offset,2);value=half;}
    return value;
}
void bus_write(Bus *b, uint32_t address, unsigned width, uint32_t value);
/* Diagnostics only: read mapped memory without faulting, logging, or touching a
   device callback. Returns 0 when no plain readable memory covers the access. */
int bus_peek(Bus *b, uint32_t address, unsigned width, uint32_t *value);
void bus_destroy(Bus *b);
void bus_fault_access(Bus *b, int write, uint32_t address, unsigned width, uint32_t value, const char *reason);
#endif
