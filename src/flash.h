#ifndef SGH_FLASH_H
#define SGH_FLASH_H
#include "bus.h"
typedef struct { uint32_t offset, size, block_size; } FlashGeometry;
typedef struct {
    Bus *bus;
    uint8_t *data;
    uint32_t base, size, block_size, command_address;
    unsigned mode, pending;
    uint16_t status, manufacturer, device;
    uint64_t programs, erases;
    uint32_t last_program, last_erase;
    FlashGeometry geometry[8];
    unsigned geometry_count;
} Flash;
/* Intel/Sharp x16 command set; operations affect host copies only. */
int flash_attach(Flash *f, Bus *bus, uint32_t base, uint16_t manufacturer, uint16_t device);
int flash_geometry(Flash *f, uint32_t offset, uint32_t size, uint32_t block_size);
#endif
