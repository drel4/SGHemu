#ifndef SGH_BUS_H
#define SGH_BUS_H
#include <stdint.h>
#include <stdio.h>
#define BUS_MAX_REGIONS 32
#define BUS_PAGE_SHIFT 16
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
    FILE *access_log; /* Optional inclusive-start/exclusive-end watch range. */
    uint32_t watch_start, watch_end;
    int fault;
    uint32_t last_address, last_value;
    unsigned last_width;
    int last_write;
    int pages_ready; /* page[] is rebuilt lazily after regions change */
    uint8_t page[1u << (32 - BUS_PAGE_SHIFT)]; /* region index + 1 when one region owns the whole 64 KiB page */
} Bus;
int bus_map(Bus *b, uint32_t base, uint32_t size, uint8_t *data, int readonly, int owned);
int bus_device(Bus *b, uint32_t base, uint32_t size, PeripheralRead read, PeripheralWrite write, void *ctx);
uint32_t bus_read(Bus *b, uint32_t address, unsigned width);
void bus_write(Bus *b, uint32_t address, unsigned width, uint32_t value);
void bus_destroy(Bus *b);
void bus_fault_access(Bus *b, int write, uint32_t address, unsigned width, uint32_t value, const char *reason);
#endif
