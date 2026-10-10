#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "Machine test line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void fixture(int identity, int overlap)
{
    FILE *f;
    static const uint8_t code[32] = {8,0,159,229,176,16,208,225,254,255,255,234,0,0,0,0,250,47,0,0};
    f = fopen("build/machine-fixture.bin", "wb"); CHECK(f != NULL);
    CHECK(fwrite(code, 1, sizeof(code), f) == sizeof(code)); CHECK(fclose(f) == 0);
    f = fopen("build/machine-fixture.cfg", "w"); CHECK(f != NULL);
    fputs("name=Machine fixture\nrom=0x1000000,machine-fixture.bin\nalias=0,0x1000000,32\nram=0x400000,0x100\nreset=0\naudio=silent\ndisplay=128,128\n", f);
    if (identity) fputs("identity16=0x2ffa,0x15\n", f);
    if (overlap) fputs("ram=0x400080,0x100\n", f);
    CHECK(fclose(f) == 0);
}
int main(void)
{
    Machine m = {0};
    fixture(0, 0); CHECK(machine_load(&m, "build/machine-fixture.cfg", NULL, NULL));
    CHECK(m.loaded && m.width == 128 && m.height == 128 && m.pixels[0] == 0);
    CHECK(machine_step(&m, 0)); CHECK(!machine_step(&m, 0));
    CHECK(m.bus.fault && m.bus.last_address == 0x2ffa && m.bus.last_width == 2 && !m.bus.last_write);
    CHECK(m.cpu.instructions == 2); CHECK(!machine_step(&m, 0) && m.cpu.instructions == 2);
    CHECK(machine_step(&m, 1) && !m.bus.fault && m.cpu.instructions == 3);
    machine_key(&m, 9, 1); machine_key(&m, 10, 1); CHECK(m.keys == ((1u << 9) | (1u << 10)));
    machine_key(&m, 9, 0); CHECK(m.keys == (1u << 10)); machine_key(&m, 100, 1); CHECK(m.keys == (1u << 10));
    machine_pattern(&m, 1); CHECK(m.pixels[0] == 0xffffff && m.pixels[127] == 0); machine_pattern(&m, 0); CHECK(m.pixels[0] == 0);
    fixture(1, 0); CHECK(machine_load(&m, "build/machine-fixture.cfg", NULL, NULL));
    CHECK(m.keys == 0 && m.cpu.instructions == 0); CHECK(machine_step(&m, 0)); CHECK(machine_step(&m, 0));
    CHECK(m.cpu.r[1] == 0x15 && m.bus.faults == 0);
    CHECK(bus_read(&m.bus, 0x2ffa, 1) == 0x15 && bus_read(&m.bus, 0x2ffb, 1) == 0);
    bus_write(&m.bus, 0x2ffa, 2, 0x16); CHECK(m.bus.fault && m.bus.last_write && bus_read(&m.bus, 0x2ffa, 2) == 0x15);
    fixture(1, 1); CHECK(!machine_load(&m, "build/machine-fixture.cfg", NULL, NULL)); CHECK(!m.loaded && m.bus.count == 0 && m.pixels == NULL);
    machine_destroy(&m); machine_destroy(&m); CHECK(!m.loaded && m.bus.count == 0);
    {
        FILE *f, *trace;
        char text[512];
        size_t n;
        uint8_t second[32] = {99};
        fixture(1, 0);
        f = fopen("build/machine-fixture.cfg", "a"); CHECK(f != NULL);
        fputs("unplaced_rom=machine-fixture.bin\n", f); CHECK(fclose(f) == 0);
        f = fopen("build/second-fixture.bin", "wb"); CHECK(f != NULL);
        CHECK(fwrite(second, 1, sizeof(second), f) == sizeof(second)); CHECK(fclose(f) == 0);
        CHECK(machine_load_pair(&m, "build/machine-fixture.cfg", NULL, "build/second-fixture.bin", NULL));
        CHECK(m.unplaced_size == 32 && m.unplaced_rom[0] == 99);
        CHECK(bus_read(&m.bus, 0x1000000, 1) == 8); /* Second image must not replace the first. */
        CHECK(bus_read(&m.bus, 0x2000000, 1) == 0 && m.bus.fault); /* No guessed mapping. */
        trace = tmpfile(); CHECK(trace != NULL);
        m.bus.access_log = trace; m.bus.watch_start = 0x2ffa; m.bus.watch_end = 0x2ffc;
        CHECK(bus_read(&m.bus, 0x2ffa, 2) == 0x15);
        bus_write(&m.bus, 0x2ffa, 2, 0x16);
        (void)bus_read(&m.bus, 0x2ffc, 2);
        CHECK(fseek(trace, 0, SEEK_SET) == 0);
        n = fread(text, 1, sizeof(text) - 1, trace); text[n] = 0;
        CHECK(strstr(text, "R16 00002ffa value=00000015 device") != NULL);
        CHECK(strstr(text, "W16 00002ffa value=00000016 device") != NULL);
        CHECK(strstr(text, "00002ffc") == NULL);
        m.bus.access_log = NULL; CHECK(fclose(trace) == 0);
        CHECK(!machine_load_pair(&m, "build/machine-fixture.cfg", NULL, "build/no-such-firmware.bin", NULL));
        CHECK(!m.loaded && !m.unplaced_rom && !m.unplaced_size && !m.bus.count);
        CHECK(machine_load(&m, "build/machine-fixture.cfg", NULL, NULL));
        CHECK(m.unplaced_size == 32 && m.unplaced_rom[0] == 8);
        machine_destroy(&m); CHECK(!m.unplaced_rom && !m.unplaced_size);
    }
    {
        FILE *f;
        fixture(1, 0);
        f = fopen("build/machine-fixture.cfg", "a"); CHECK(f != NULL);
        fputs("intc=0x40100\nregister32=0x40900,0\nirq_gate=0x40902,0x80\n", f); CHECK(fclose(f) == 0);
        CHECK(machine_load(&m, "build/machine-fixture.cfg", NULL, NULL));
        CHECK(m.irq_gate_register != NULL);
        m.intc.enabled = m.intc.pending = 8; m.cpu.cpsr &= ~0x80u;
        CHECK(machine_step(&m, 0)); CHECK(m.cpu.instructions == 1 && m.cpu.r[15] == 4 && !m.cpu.irq);
        CHECK(m.intc.pending == 8); /* Critical section must not discard the source. */
        bus_write(&m.bus, 0x40902, 2, 0x80);
        CHECK(machine_step(&m, 0)); CHECK(m.cpu.instructions == 1 && m.cpu.r[15] == 0x18);
        CHECK((m.cpu.cpsr & 31) == 0x12 && m.cpu.r[14] == 8 && m.intc.pending == 8);
        CHECK(m.bus.faults == 0);
        fixture(1, 0);
        f = fopen("build/machine-fixture.cfg", "a"); CHECK(f != NULL);
        fputs("intc=0x40100\nirq_gate=0x40902,0x80\n", f); CHECK(fclose(f) == 0);
        CHECK(!machine_load(&m, "build/machine-fixture.cfg", NULL, NULL)); CHECK(!m.loaded && !m.bus.count);
        machine_destroy(&m);
    }
    {
        FILE *f;uint64_t before;
        fixture(1,0);f=fopen("build/machine-fixture.cfg","a");CHECK(f!=NULL);
        fputs("intc=0x40100\nclock=0x40b00,3900000\nclock_mode=0x0b01,23400000,46800000\nperiodic_time_irq=26,1000\n",f);CHECK(fclose(f)==0);
        CHECK(machine_load(&m,"build/machine-fixture.cfg",NULL,NULL));CHECK(m.clock.attached&&m.periodic_time);
        CHECK(m.clock.arm_hz==3900000&&m.clock.dsp_hz==3900000);
        bus_write(&m.bus,0x40b02,2,0x0b01);bus_write(&m.bus,0x40b04,2,15);
        CHECK(m.clock.arm_hz==23400000&&m.clock.dsp_hz==46800000&&!m.clock.unknown);
        CHECK(bus_read(&m.bus,0x40b02,2)==0x0b01&&bus_read(&m.bus,0x40b04,1)==15);
        CHECK(machine_step(&m,0));CHECK(m.cpu.clock_cycles==3&&m.clock.nanoseconds==128);
        CHECK(machine_step(&m,0));CHECK(m.clock.nanoseconds==256);
        m.intc.enabled=1u<<26;CHECK(machine_step(&m,0));
        while(m.clock.nanoseconds<1200)CHECK(machine_step(&m,0));
        CHECK(m.intc.events==1&&(m.intc.pending&(1u<<26)));
        before=m.clock.nanoseconds;bus_write(&m.bus,0x40b04,2,0);CHECK(machine_step(&m,0));
        CHECK(m.clock.arm_hz==3900000&&m.clock.nanoseconds-before>=769);
        bus_write(&m.bus,0x40b02,2,0xdead);bus_write(&m.bus,0x40b04,2,15);
        CHECK(m.clock.unknown&&m.clock.arm_hz==3900000);CHECK(m.bus.faults==0);machine_destroy(&m);
    }
    printf("Machine/frontend backend: %u checks passed.\n", checks); return 0;
}
