#ifndef SGH_ARM7_H
#define SGH_ARM7_H
#include "bus.h"
#define ARM_N 0x80000000u
#define ARM_Z 0x40000000u
#define ARM_C 0x20000000u
#define ARM_V 0x10000000u
#define ARM_T 0x20u
typedef struct {
    uint32_t r[16], cpsr;
    uint32_t bank[6][2], spsr[6], user_hi[5], fiq_hi[5];
    uint64_t instructions, cycles;
    uint64_t clock_cycles; /* ARM7 core cycles, excluding external memory waits. */
    uint32_t last_pc, last_opcode;
    int halted, irq, fiq;
    const char *error;
    Bus *bus;
} Arm7;
void arm7_reset(Arm7 *cpu, Bus *bus, uint32_t pc);
int arm7_step(Arm7 *cpu);
int arm7_set_cpsr(Arm7 *cpu, uint32_t value);
#endif
