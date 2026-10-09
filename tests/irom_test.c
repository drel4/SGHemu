#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "IROM test line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void reset(Arm7 *c, Bus *b, uint32_t pc)
{
    unsigned i;
    arm7_reset(c, b, pc); c->cpsr |= ARM_T | ARM_N | ARM_C;
    for (i = 3; i < 15; ++i) c->r[i] = 0x12340000 + i;
    c->r[14] = 0x2001;
}
static void fixture(const char *services)
{
    FILE *f = fopen("build/irom-fixture.bin", "wb");
    static const uint8_t bytes[32] = {0xfe,0xff,0xff,0xea};
    CHECK(f != NULL); CHECK(fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes)); CHECK(fclose(f) == 0);
    f = fopen("build/irom-fixture.cfg", "w"); CHECK(f != NULL);
    fputs("rom=0x1000000,irom-fixture.bin\nram=0x1000,0x200\nreset=0x1000000\nintc=0x40100\naudio=silent\n", f);
    fputs(services, f); CHECK(fclose(f) == 0);
}
int main(void)
{
    Bus bus = {0}; Arm7 c; Irom rom = {0}; Machine m = {0};
    uint8_t ram[512], source[128]; unsigned i; uint32_t flags;
    memset(ram, 0xcc, sizeof(ram)); for (i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 13);
    CHECK(bus_map(&bus, 0x1000, sizeof(ram), ram, 0, 0)); CHECK(bus_map(&bus, 0x2000, sizeof(source), source, 1, 0));
    CHECK(irom_add(&rom, 0x8077c, "memcpy")); CHECK(!irom_add(&rom, 0x8077c, "memcpy"));
    CHECK(!irom_add(&rom, 0x81fe1, "version")); CHECK(!irom_add(&rom, 0x81fe0, "unknown"));
    CHECK(irom_add(&rom, 0x81fe0, "version")); rom.version = 0x1234; rom.version_return = 0x2000; rom.version_configured = 1;
    reset(&c, &bus, 0x8077c); flags = c.cpsr;
    c.r[0] = 0x1001; c.r[1] = 0x2003; c.r[2] = 17;
    CHECK(irom_step(&rom, &c) == 1); CHECK(!memcmp(ram + 1, source + 3, 17)); CHECK(ram[0] == 0xcc && ram[18] == 0xcc);
    CHECK(c.r[0] == 0x1001 && c.r[15] == 0x2000 && c.cpsr == flags && c.instructions == 1 && c.cycles == 1);
    for (i = 3; i < 14; ++i) CHECK(c.r[i] == 0x12340000 + i);
    reset(&c, &bus, 0x8077c); c.r[0] = 0x1020; c.r[1] = 0x1001; c.r[2] = 17; c.r[14] = 0x2006;
    CHECK(irom_step(&rom, &c) == 1); CHECK(!(c.cpsr & ARM_T) && c.r[15] == 0x2004); CHECK(!memcmp(ram + 32, ram + 1, 17));
    reset(&c, &bus, 0x8077c); c.r[0] = c.r[1] = 0xffffffff; c.r[2] = 0;
    CHECK(irom_step(&rom, &c) == 1 && c.r[0] == 0xffffffff); /* Zero bytes require no memory access. */
    reset(&c, &bus, 0x8077c); c.r[0] = 0x1000; c.r[1] = 0x1001; c.r[2] = 8;
    CHECK(irom_step(&rom, &c) == -1 && c.halted && !c.instructions); CHECK(ram[0] == 0xcc);
    reset(&c, &bus, 0x8077c); c.r[0] = 0x2000; c.r[1] = 0x1000; c.r[2] = 1;
    CHECK(irom_step(&rom, &c) == -1 && source[0] == 0); /* Cannot write ROM. */
    reset(&c, &bus, 0x8077c); c.r[0] = 0x1000; c.r[1] = 0xfffffff0; c.r[2] = 32;
    CHECK(irom_step(&rom, &c) == -1 && ram[0] == 0xcc);
    reset(&c, &bus, 0x81fe0); c.r[0] = 0x1080; c.r[1] = 0xffffffff; c.r[2] = 0xffffffff;
    flags = c.cpsr; CHECK(irom_step(&rom, &c) == 1);
    CHECK(ram[128] == 0x34 && ram[129] == 0x12 && ram[130] == 0xcc && ram[131] == 0xcc);
    CHECK(c.r[1] == 0xffffffff && c.r[2] == 0xffffffff && c.cpsr == flags);
    reset(&c, &bus, 0x81fe0); c.r[0] = 0x1080; c.r[14] = 0x2041;
    CHECK(irom_step(&rom, &c) == -1 && ram[128] == 0x34); /* Unverified caller cannot obtain fake BIST results. */
    reset(&c, &bus, 0x81fe0); c.r[0] = 0x1081; CHECK(irom_step(&rom, &c) == -1);
    reset(&c, &bus, 0x81fe0); rom.version_configured = 0; CHECK(irom_step(&rom, &c) == -1);
    reset(&c, &bus, 0x80020); CHECK(irom_step(&rom, &c) == 0 && !c.halted && !c.instructions);
    reset(&c, &bus, 0x8077c); c.cpsr &= ~ARM_T; CHECK(irom_step(&rom, &c) == -1);
    fixture("irom_version=0x81fe0,0xffff,0x1000000\nirom_service=0x8077c,memcpy\n");
    CHECK(machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    m.cpu.r[15] = 0x81fe0; m.cpu.r[0] = 0x1080; m.cpu.r[14] = 0x1000001; m.cpu.cpsr &= ~0x80u;
    m.intc.pending = m.intc.enabled = 8;
    CHECK(machine_step(&m, 0)); CHECK(m.cpu.r[15] == 0x18 && m.cpu.instructions == 0 && m.irom.services[0].calls == 0);
    CHECK(machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    m.cpu.r[15] = 0x81fe0; m.cpu.r[0] = 0x1080; m.cpu.r[14] = 0x1000001; m.cpu.cpsr |= ARM_T;
    CHECK(machine_step(&m, 0)); CHECK(bus_read(&m.bus, 0x1080, 2) == 0xffff && m.cpu.r[15] == 0x1000000 && m.irom.services[0].calls == 1);
    fixture("irom_service=0x81fe0,version\n"); CHECK(!machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    fixture("irom_service=0x1000000,memcpy\n"); CHECK(!machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    fixture("irom_version=0x81fe0,0x10000,0x1000000\n"); CHECK(!machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    fixture("irom_version=0x81fe0,0xffff,0x1000001\n"); CHECK(!machine_load(&m, "build/irom-fixture.cfg", NULL, NULL));
    machine_destroy(&m); bus_destroy(&bus);
    printf("Internal-ROM HLE: %u checks passed.\n", checks); return 0;
}
