#include "irom.h"
#include <string.h>
int irom_add(Irom *rom, uint32_t address, const char *name)
{
    unsigned i, kind;
    if (address & 1 || rom->count == IROM_MAX_SERVICES) return 0;
    if (!strcmp(name, "memcpy")) kind = IROM_MEMCPY;
    else if (!strcmp(name, "version")) kind = IROM_VERSION;
    else return 0;
    for (i = 0; i < rom->count; ++i) if (rom->services[i].address == address) return 0;
    if(!rom->count||address<rom->first_address)rom->first_address=address;
    if(!rom->count||address>rom->last_address)rom->last_address=address;
    rom->services[rom->count].address = address; rom->services[rom->count++].kind = kind;
    return 1;
}
/* HLE accepts ordinary memory, never MMIO or flash-command destinations.
   Validate all ranges before modifying the guest; do not silently continue
   through an invalid pointer just because bring-up ignores unknown MMIO. */
static int memory(Bus *bus, uint32_t address, uint32_t size, int write)
{
    uint64_t end = (uint64_t)address + size;
    unsigned i;
    if (end > UINT64_C(0x100000000)) return 0;
    if (!size) return 1;
    for (i = 0; i < bus->count; ++i) {
        Region *r = &bus->regions[i];
        if (address >= r->base && end <= (uint64_t)r->base + r->size && r->data && !r->read &&
            (!write || (!r->readonly && !r->write))) return 1;
        /* ROM flash has a read callback; use bus reads so its current mode
           remains observable, but never treat a device-only region as RAM. */
        if (!write && address >= r->base && end <= (uint64_t)r->base + r->size && r->data && r->readonly) return 1;
    }
    return 0;
}
static int bad(Arm7 *c, const char *reason)
{
    c->halted = 1; c->error = reason; return -1;
}
int irom_step(Irom *rom, Arm7 *c)
{
    IromService *service = NULL;
    unsigned i;
    uint32_t destination = c->r[0], source = c->r[1], size = c->r[2], result, return_address = c->r[14];
    for (i = 0; i < rom->count; ++i) if (rom->services[i].address == c->r[15]) { service = &rom->services[i]; break; }
    if (!service) return 0;
    c->last_pc = c->r[15]; c->last_opcode = 0;
    c->bus->pc = c->last_pc; c->bus->ticks = c->cycles;
    if (!(c->cpsr & ARM_T)) return bad(c, "internal-ROM service: expected Thumb entry");
    if (service->kind == IROM_MEMCPY) {
        if (size > 32u * 1024 * 1024 || !memory(c->bus, source, size, 0) || !memory(c->bus, destination, size, 1))
            return bad(c, "internal-ROM memcpy: invalid memory range");
        if (size && destination != source && (uint64_t)destination < (uint64_t)source + size &&
            (uint64_t)source < (uint64_t)destination + size) return bad(c, "internal-ROM memcpy: overlapping buffers");
        if (destination != source) for (i = 0; i < size; ++i)
            bus_write(c->bus, destination + i, 1, bus_read(c->bus, source + i, 1));
        result = destination;
    } else {
        if (!rom->version_configured || return_address != (rom->version_return | 1u))
            return bad(c, "internal-ROM version query: unverified caller contract");
        if ((destination & 1) || !memory(c->bus, destination, 2, 1))
            return bad(c, "internal-ROM version query: invalid output pointer");
        /* UHGetIromVersion consumes only this halfword. Its other two output
           buffers are discarded. Do not invent their widths or BIST results.
           Restrict this HLE to the decoded caller; other BIST uses must stop. */
        bus_write(c->bus, destination, 2, rom->version);
        result = destination; /* Scalar return is not consumed by this caller. */
    }
    if (c->bus->fault) return bad(c, "internal-ROM service: bus fault");
    c->r[0] = result;
    /* ARMv4T interworking return, equivalent to BX LR. Flags and banked
       registers are preserved; the shared CPU interpreter is unchanged. */
    c->cpsr = (c->cpsr & ~ARM_T) | ((return_address & 1) ? ARM_T : 0);
    c->r[15] = return_address & ((return_address & 1) ? ~1u : ~3u);
    ++service->calls; ++c->instructions; ++c->cycles; /* One atomic scheduler step, not cycle accurate. */
    return 1;
}
