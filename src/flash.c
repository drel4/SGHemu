#include "flash.h"
#include <string.h>
enum { ARRAY, STATUS, IDENTIFY };
enum { NONE, PROGRAM, ERASE, LOCK };
static uint32_t read_flash(void *context, uint32_t offset, unsigned width)
{
    Flash *f = context;
    uint32_t value = 0;
    unsigned i;
    if (f->mode == STATUS) return f->status;
    if (f->mode == IDENTIFY) {
        switch (offset & 0xff) {
        case 0: return f->manufacturer;
        case 2: return f->device;
        case 4: return 0; /* Blocks initially unlocked; lock persistence not implemented. */
        default: return 0xffff;
        }
    }
    for (i = 0; i < width; ++i) value |= (uint32_t)f->data[offset + i] << (i * 8);
    return value;
}
static void write_flash_command(void *context, uint32_t offset, unsigned width, uint32_t value)
{
    Flash *f = context;
    unsigned i;
    uint32_t block, block_size;
    if (width != 2 || (offset & 1)) {
        bus_fault_access(f->bus, 1, f->base + offset, width, value, "unsupported x16 flash access"); return;
    }
    if (f->pending == PROGRAM) {
        ++f->programs; f->last_program = f->base + offset;
        if (offset != f->command_address) f->status = 0x90;
        else {
            for (i = 0; i < 2; ++i) f->data[offset + i] &= (uint8_t)(value >> (i * 8));
            f->status = 0x80;
        }
        f->pending = NONE; f->mode = STATUS; return;
    }
    if (f->pending == ERASE && value == 0xd0) {
        ++f->erases; f->last_erase = f->base + f->command_address;
        block_size = f->block_size;
        block = f->command_address / block_size * block_size;
        for (i = 0; i < f->geometry_count; ++i) {
            const FlashGeometry *g = &f->geometry[i];
            if (f->command_address >= g->offset && f->command_address - g->offset < g->size) {
                block_size = g->block_size;
                block = g->offset + (f->command_address - g->offset) / block_size * block_size;
                break;
            }
        }
        memset(f->data + block, 255, f->size - block < block_size ? f->size - block : block_size);
        f->pending = NONE; f->status = 0x80; f->mode = STATUS; return;
    }
    if (f->pending == LOCK && (value == 0xd0 || value == 1 || value == 4)) {
        /* Partition and lock control accepted provisionally for bring-up. */
        f->pending = NONE; f->status = 0x80; f->mode = STATUS; return;
    }
    switch (value) {
    case 0xff: f->mode = ARRAY; f->pending = NONE; break;
    case 0x70: f->mode = STATUS; break;
    case 0x90: f->mode = IDENTIFY; break;
    case 0x50: f->status = 0x80; break;
    case 0x40: case 0x10: f->pending = PROGRAM; f->command_address = offset; break;
    case 0x20: f->pending = ERASE; f->command_address = offset; break;
    case 0x60: f->pending = LOCK; f->command_address = offset; break;
    case 0xb0: case 0xd0: f->status = 0x80; f->mode = STATUS; break;
    default: bus_fault_access(f->bus, 1, f->base + offset, width, value, "unimplemented flash command"); break;
    }
}
static void write_flash(void *context, uint32_t offset, unsigned width, uint32_t value)
{
    Flash *f = context;
    write_flash_command(context, offset, width, value);
    /* Array mode reads are plain memory reads; skip the callback for them.
       Status and identify modes still go through read_flash(). */
    if (f->region) f->region->fast = f->mode == ARRAY;
}
int flash_geometry(Flash *f, uint32_t offset, uint32_t size, uint32_t block_size)
{
    unsigned i;
    FlashGeometry *g;
    if (f->geometry_count == 8 || !size || block_size < 2 || (block_size & (block_size - 1)) ||
        offset % block_size || size % block_size || offset % f->block_size || size % f->block_size ||
        (uint64_t)offset + size > f->size) return 0;
    for (i = 0; i < f->geometry_count; ++i) {
        g = &f->geometry[i];
        if ((uint64_t)offset < (uint64_t)g->offset + g->size && (uint64_t)g->offset < (uint64_t)offset + size) return 0;
    }
    g = &f->geometry[f->geometry_count++]; g->offset = offset; g->size = size; g->block_size = block_size;
    return 1;
}
int flash_attach(Flash *f, Bus *bus, uint32_t base, uint16_t manufacturer, uint16_t device)
{
    unsigned i;
    for (i = 0; i < bus->count; ++i) {
        Region *r = &bus->regions[i];
        if (r->base == base && r->data && r->readonly && !r->read) {
            memset(f, 0, sizeof(*f)); f->bus = bus; f->data = r->data;
            f->base = base; f->size = r->size; f->block_size = 0x10000;
            f->manufacturer = manufacturer; f->device = device; f->status = 0x80;
            r->read = read_flash; r->write = write_flash; r->context = f;
            f->region = r; r->fast = 1; /* power-up state is array mode */
            return 1;
        }
    }
    return 0;
}
