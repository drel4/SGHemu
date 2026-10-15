#include "arm7.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t memory[65536];
static Bus bus;
static Arm7 cpu;
static unsigned checks;
static Arm7Cache *cache;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s (PC=%08x opcode=%08x error=%s)\n", __FILE__, __LINE__, #x, cpu.last_pc, cpu.last_opcode, cpu.error ? cpu.error : "none"); exit(1); } } while (0)
static void setup(void)
{
    memset(&bus, 0, sizeof(bus)); memset(memory, 0, sizeof(memory));
    CHECK(bus_map(&bus, 0, sizeof(memory), memory, 0, 0)); arm7_reset(&cpu, &bus, 0);cpu.cache=cache;
}
static void word(uint32_t at, uint32_t op) { bus_write(&bus, at, 4, op); }
static void half(uint32_t at, uint32_t op) { bus_write(&bus, at, 2, op); }
static void step(void) { CHECK(arm7_step(&cpu)); CHECK(!bus.fault); }
static void run_arm(uint32_t op) { cpu.r[15] = 0; word(0, op); step(); }
static void run_thumb(uint32_t op) { cpu.r[15] = 0; cpu.cpsr |= ARM_T; half(0, op); step(); }
static void arithmetic(void)
{
    setup(); run_arm(0xe3a0002a); CHECK(cpu.r[0] == 42); /* MOV */
    run_arm(0xe2901001); CHECK(cpu.r[1] == 43 && !(cpu.cpsr & (ARM_Z | ARM_C | ARM_V | ARM_N)));
    cpu.r[0] = 0xffffffffu; run_arm(0xe2901001); CHECK(cpu.r[1] == 0 && (cpu.cpsr & (ARM_C | ARM_Z)) == (ARM_C | ARM_Z));
    cpu.r[0] = 0x7fffffffu; run_arm(0xe2901001); CHECK(cpu.r[1] == 0x80000000u && (cpu.cpsr & (ARM_N | ARM_V)) == (ARM_N | ARM_V));
    cpu.r[0] = 0; run_arm(0xe2501001); CHECK(cpu.r[1] == 0xffffffffu && !(cpu.cpsr & ARM_C));
    cpu.r[0] = 0x80000000u; run_arm(0xe2501001); CHECK(cpu.r[1] == 0x7fffffffu && (cpu.cpsr & (ARM_V | ARM_C)) == (ARM_V | ARM_C));
    cpu.r[0] = 0xffffffffu; cpu.r[1] = 0; cpu.cpsr |= ARM_C; run_arm(0xe0b02001); CHECK(cpu.r[2] == 0 && (cpu.cpsr & ARM_C));
    cpu.r[0] = 0; cpu.r[1] = 0; cpu.cpsr &= ~ARM_C; run_arm(0xe0d02001); CHECK(cpu.r[2] == 0xffffffffu && !(cpu.cpsr & ARM_C));
    cpu.cpsr |= ARM_Z; run_arm(0x13a00055); CHECK(cpu.r[0] == 0); /* conditional MOVNE skipped */
    run_arm(0x03a00055); CHECK(cpu.r[0] == 0x55);
    run_arm(0xe1a0100f); CHECK(cpu.r[1] == 8); /* PC pipeline */
    cpu.r[0] = 1; cpu.r[1] = 32; run_arm(0xe1b02110); CHECK(cpu.r[2] == 0 && (cpu.cpsr & ARM_C)); /* LSL Rs */
    cpu.r[1] = 33; run_arm(0xe1b02110); CHECK(cpu.r[2] == 0 && !(cpu.cpsr & ARM_C));
    cpu.r[0] = 0x80000001u; run_arm(0xe1b02020); CHECK(cpu.r[2] == 0 && (cpu.cpsr & ARM_C)); /* LSR #32 */
    run_arm(0xe1b02040); CHECK(cpu.r[2] == UINT32_MAX && (cpu.cpsr & ARM_C)); /* ASR #32 */
    cpu.cpsr &= ~ARM_C; run_arm(0xe1b02060); CHECK(cpu.r[2] == 0x40000000u && (cpu.cpsr & ARM_C)); /* RRX */
    cpu.r[0] = 3; cpu.r[1] = 7; run_arm(0xe0020190); CHECK(cpu.r[2] == 21); /* MUL */
    cpu.r[0] = 0xffffffffu; cpu.r[1] = 2; run_arm(0xe0832190); CHECK(cpu.r[2] == 0xfffffffeu && cpu.r[3] == 1); /* UMULL */
    run_arm(0xe0c32190); CHECK(cpu.r[2] == 0xfffffffeu && cpu.r[3] == 0xffffffffu); /* SMULL */
}
static void memory_ops(void)
{
    setup(); cpu.r[0] = 0x100; cpu.r[1] = 0x44332211;
    run_arm(0xe5801000); CHECK(bus_read(&bus, 0x100, 4) == 0x44332211);
    run_arm(0xe5902001); CHECK(cpu.r[2] == 0x11443322); /* unaligned word load rotates */
    run_arm(0xe5d02002); CHECK(cpu.r[2] == 0x33);
    run_arm(0xe1d020b0); CHECK(cpu.r[2] == 0x2211);
    bus_write(&bus, 0x100, 2, 0x80ff); run_arm(0xe1d020d0); CHECK(cpu.r[2] == 0xffffffffu);
    run_arm(0xe1d020f0); CHECK(cpu.r[2] == 0xffff80ffu);
    run_arm(0xe1d020f1); CHECK(cpu.r[2] == 0xffffff80u); /* odd signed-halfword is signed byte */
    cpu.r[0] = 0x100; run_arm(0xe4902004); CHECK(cpu.r[0] == 0x104 && cpu.r[2] == 0x443380ff);
    cpu.r[0] = 0x100; cpu.r[1] = 0x12345678; run_arm(0xe1002091); CHECK(cpu.r[2] == 0x443380ff && bus_read(&bus, 0x100, 4) == 0x12345678); /* SWP */
    cpu.r[0] = 0x200; cpu.r[1] = 11; cpu.r[2] = 22; run_arm(0xe8a00006);
    CHECK(cpu.r[0] == 0x208 && bus_read(&bus, 0x200, 4) == 11 && bus_read(&bus, 0x204, 4) == 22);
    cpu.r[0] = 0x208; cpu.r[1] = 0; cpu.r[2] = 0; run_arm(0xe9300006); CHECK(cpu.r[0] == 0x200 && cpu.r[1] == 11 && cpu.r[2] == 22);
    cpu.r[0] = 0x300; run_arm(0xe8800000); CHECK(bus_read(&bus, 0x300, 4) == 12); /* ARM7 empty-list PC */
    run_arm(0xe8a00000); CHECK(cpu.r[0] == 0x340);
}
static void control_and_banks(void)
{
    setup(); run_arm(0xeb000002); CHECK(cpu.r[15] == 16 && cpu.r[14] == 4);
    cpu.r[0] = 0x101; run_arm(0xe12fff10); CHECK(cpu.r[15] == 0x100 && (cpu.cpsr & ARM_T));
    cpu.r[0] = 0x200; half(0x100, 0x4700); step(); CHECK(cpu.r[15] == 0x200 && !(cpu.cpsr & ARM_T));
    cpu.r[13] = 0xabc; cpu.r[14] = 0xdef; cpu.r[8] = 88;
    CHECK(arm7_set_cpsr(&cpu, 0xd1)); CHECK(cpu.r[13] == 0 && cpu.r[8] == 0);
    cpu.r[13] = 0x111; cpu.r[8] = 111; CHECK(arm7_set_cpsr(&cpu, 0xd3)); CHECK(cpu.r[13] == 0xabc && cpu.r[8] == 88);
    CHECK(arm7_set_cpsr(&cpu, 0xd1)); CHECK(cpu.r[13] == 0x111 && cpu.r[8] == 111);
    CHECK(arm7_set_cpsr(&cpu, 0xd3));
    run_arm(0xef000000); CHECK(cpu.r[15] == 8 && cpu.r[14] == 4 && cpu.spsr[3] == 0xd3);
    cpu.spsr[3] = 0x30; cpu.r[14] = 0x103; run_arm(0xe1b0f00e); CHECK(cpu.r[15] == 0x102 && cpu.cpsr == 0x30);
    setup(); CHECK(arm7_set_cpsr(&cpu, 0x10)); cpu.r[13] = 0x900;
    CHECK(arm7_set_cpsr(&cpu, 0xd3)); cpu.r[13] = 0x800; cpu.spsr[3] = 0x30; word(0x800, 0x101);
    run_arm(0xe8fd8000); CHECK(cpu.cpsr == 0x30 && cpu.r[15] == 0x100 && cpu.r[13] == 0x900 && cpu.bank[3][0] == 0x804);
    setup(); cpu.cpsr = 0x13; cpu.irq = 1; step(); CHECK(cpu.r[15] == 0x18 && cpu.r[14] == 4 && cpu.spsr[2] == 0x13 && (cpu.cpsr & 31) == 0x12);
    cpu.irq = 0; word(0x18, 0xe25ef004); step(); CHECK(cpu.r[15] == 0 && cpu.cpsr == 0x13);
    setup(); cpu.cpsr = 0x33; cpu.r[15] = 0x100; cpu.fiq = 1; step(); CHECK(cpu.r[15] == 0x1c && cpu.r[14] == 0x104 && cpu.spsr[1] == 0x33 && (cpu.cpsr & 31) == 0x11);
    setup(); cpu.r[0] = 0xd2; run_arm(0xe121f000); CHECK((cpu.cpsr & 255) == 0xd2); /* MSR CPSR_c */
    run_arm(0xe10f1000); CHECK(cpu.r[1] == cpu.cpsr);
    cpu.r[0] = 0x30; run_arm(0xe16ff000); CHECK(cpu.spsr[2] == 0x30); /* SPSR including Thumb */
    setup(); CHECK(arm7_set_cpsr(&cpu, 0x10)); cpu.r[0] = 0xf00000d3; run_arm(0xe12ff000); CHECK(cpu.cpsr == 0xf0000010); /* user flags only */
}
static void thumb_programs(void)
{
    setup(); run_thumb(0x202a); CHECK(cpu.r[0] == 42);
    run_thumb(0x3001); CHECK(cpu.r[0] == 43); run_thumb(0x3801); CHECK(cpu.r[0] == 42);
    run_thumb(0x282a); CHECK(cpu.cpsr & ARM_Z);
    run_thumb(0xd001); CHECK(cpu.r[15] == 6); /* BEQ */
    run_thumb(0xe7fe); CHECK(cpu.r[15] == 0); /* B self */
    cpu.r[0] = 0x100; cpu.r[1] = 0x89abcdef; run_thumb(0x6001); CHECK(bus_read(&bus, 0x100, 4) == 0x89abcdef);
    run_thumb(0x6802); CHECK(cpu.r[2] == 0x89abcdef);
    cpu.r[13] = 0x800; cpu.r[0] = 10; cpu.r[1] = 20; cpu.r[14] = 0x101;
    run_thumb(0xb503); CHECK(cpu.r[13] == 0x7f4); cpu.r[0] = 0; cpu.r[1] = 0;
    run_thumb(0xbd03); CHECK(cpu.r[13] == 0x800 && cpu.r[0] == 10 && cpu.r[1] == 20 && cpu.r[15] == 0x100 && (cpu.cpsr & ARM_T));
    run_thumb(0xf000); CHECK(cpu.r[14] == 4); half(2, 0xf802); step(); CHECK(cpu.r[15] == 8 && cpu.r[14] == 5);
    cpu.r[0] = 0x100; cpu.r[1] = 1; cpu.r[2] = 2; run_thumb(0xc006); CHECK(cpu.r[0] == 0x108);
    cpu.r[0] = 0x100; cpu.r[1] = cpu.r[2] = 0; run_thumb(0xc806); CHECK(cpu.r[0] == 0x108 && cpu.r[1] == 1 && cpu.r[2] == 2);
    cpu.r[0] = 0x80000001u; cpu.r[1] = 32; run_thumb(0x40c8); CHECK(cpu.r[0] == 0 && (cpu.cpsr & ARM_C));
    run_thumb(0xdf00); CHECK(cpu.r[15] == 8 && cpu.r[14] == 2 && !(cpu.cpsr & ARM_T) && (cpu.spsr[3] & ARM_T));
}
static void faults(void)
{
    setup(); bus_write(&bus, 0xffff, 4, 1); CHECK(bus.fault && bus.faults == 1);
    bus.fault = 0; CHECK(bus_read(&bus, 0x10000, 1) == 0 && bus.fault);
    setup(); CHECK(!bus_map(&bus, 0x100, 20, memory, 0, 0));
    setup(); word(0, 0xe12fff30); CHECK(!arm7_step(&cpu) && cpu.halted); /* ARMv5 BLX must not execute as data processing */
    setup(); cpu.cpsr |= ARM_T; half(0, 0xbe00); CHECK(!arm7_step(&cpu)); /* ARMv5 BKPT */
}
static void clock_timing(void)
{
    uint64_t previous;
    setup();run_arm(0xe3a00001);CHECK(cpu.clock_cycles==1);
    previous=cpu.clock_cycles;run_arm(0xea000000);CHECK(cpu.clock_cycles-previous==3);
    previous=cpu.clock_cycles;cpu.cpsr&=~ARM_Z;run_arm(0x05901000);CHECK(cpu.clock_cycles-previous==1);
    previous=cpu.clock_cycles;cpu.r[0]=0x100;run_arm(0xe5901000);CHECK(cpu.clock_cycles-previous==3);
    previous=cpu.clock_cycles;run_arm(0xe5801000);CHECK(cpu.clock_cycles-previous==2);
    previous=cpu.clock_cycles;cpu.r[0]=3;cpu.r[1]=7;run_arm(0xe0020190);CHECK(cpu.clock_cycles-previous==2);
    previous=cpu.clock_cycles;cpu.r[1]=0x12345678;run_arm(0xe0020190);CHECK(cpu.clock_cycles-previous==5);
    previous=cpu.clock_cycles;cpu.r[0]=0x100;run_arm(0xe8900006);CHECK(cpu.clock_cycles-previous==4);
    setup();previous=cpu.clock_cycles;cpu.r[0]=0x100;run_thumb(0x6801);CHECK(cpu.clock_cycles-previous==3);
    previous=cpu.clock_cycles;run_thumb(0x6001);CHECK(cpu.clock_cycles-previous==2);
    previous=cpu.clock_cycles;run_thumb(0xe7fe);CHECK(cpu.clock_cycles-previous==3);
    previous=cpu.clock_cycles;cpu.cpsr&=~ARM_Z;run_thumb(0xd000);CHECK(cpu.clock_cycles-previous==1);
    previous=cpu.clock_cycles;cpu.cpsr|=ARM_Z;run_thumb(0xd000);CHECK(cpu.clock_cycles-previous==3);
    previous=cpu.clock_cycles;cpu.irq=1;cpu.cpsr&=~0x80u;step();CHECK(cpu.clock_cycles-previous==3);
}
int main(void)
{
    arithmetic(); memory_ops(); control_and_banks(); thumb_programs(); faults();clock_timing();
    cache=arm7_cache_create();CHECK(cache);
    arithmetic(); memory_ops(); control_and_banks(); thumb_programs(); faults();clock_timing();
    arm7_cache_destroy(cache);cache=NULL;cpu.cache=NULL;
    printf("ARM7TDMI: %u checks passed (instruction programs, flags, memory, banks, IRQ/FIQ, interworking).\n", checks); return 0;
}
