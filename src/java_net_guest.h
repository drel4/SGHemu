#ifndef SGH_JAVA_NET_GUEST_H
#define SGH_JAVA_NET_GUEST_H

#include "java_net.h"

/* Shared by the Windows bridge and the Linux offline backend. A buffer must
   fit inside a single guest memory region; device registers are not buffers. */
static int memory(Bus *bus, uint32_t address, unsigned size, int write)
{
    unsigned i;
    if (!size) return 1;
    for (i = 0; i < bus->count; ++i) {
        Region *region = &bus->regions[i];
        if (region->data && !region->read && (!write || !region->readonly) &&
            address >= region->base && (uint64_t)address + size <= (uint64_t)region->base + region->size)
            return 1;
    }
    return 0;
}

/* Simulate returning from the firmware function. Bit 0 of the link register
   (r14) selects Thumb or ARM mode; r0 holds the result. */
static int returned(Arm7 *cpu, uint32_t value)
{
    uint32_t lr = cpu->r[14];
    cpu->r[0] = value;
    cpu->cpsr = (cpu->cpsr & ~ARM_T) | ((lr & 1) ? ARM_T : 0);
    cpu->r[15] = lr & ((lr & 1) ? ~1u : ~3u);
    ++cpu->instructions;
    ++cpu->cycles;
    return 1;
}

static int error_return(Arm7 *cpu, uint32_t pointer, unsigned error, uint32_t value)
{
    if (pointer && memory(cpu->bus, pointer, 2, 1))
        bus_write(cpu->bus, pointer, 2, error);
    return returned(cpu, value);
}

static unsigned java_net_operation(const JavaNetwork *n, const Arm7 *cpu)
{
    unsigned op;
    if (!(cpu->cpsr & ARM_T) || !java_net_may_call(n, cpu->r[15]))
        return JN_COUNT;
    for (op = 0; op < JN_COUNT; ++op)
        if (op != JN_NOTIFY && n->entry[op] == cpu->r[15])
            break;
    return op;
}

static int java_net_unavailable(Arm7 *cpu, unsigned op)
{
    uint32_t pointer = 0;

    /* The 16-bit error output is in a different argument for each call. */
    switch (op) {
    case JN_CLOSE:
    case JN_AVAILABLE:
        pointer = cpu->r[1];
        break;
    case JN_SOCKET:
    case JN_CONNECT:
    case JN_BIND:
    case JN_WRITE:
    case JN_READ:
    case JN_LOOKUP:
        pointer = cpu->r[3];
        break;
    }
    return error_return(cpu, pointer, 0x78, UINT32_MAX); /* Guest offline error, return -1. */
}

#endif
