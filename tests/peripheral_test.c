#include "lcd.h"
#include "flash.h"
#include "intc.h"
#include "pmic.h"
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "Peripheral test line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static Bus bus;
static void cmd(unsigned v) { bus_write(&bus, 0xc00000, 2, v); }
static void pins(unsigned sda, unsigned scl)
{
    bus_write(&bus, 0x40300, 2, sda); bus_write(&bus, 0x40302, 2, scl);
}
static void start(void) { pins(1, 0); pins(1, 1); pins(0, 1); pins(0, 0); }
static void stop(void) { pins(0, 0); pins(0, 1); pins(1, 1); }
static unsigned send(unsigned value)
{
    unsigned n, ack;
    for (n = 8; n; --n) {
        unsigned bit = (value >> (n - 1)) & 1;
        pins(bit, 0); CHECK(((bus_read(&bus, 0x40300, 2) >> 1) & 1) == bit);
        pins(bit, 1); pins(bit, 0);
    }
    pins(1, 0); pins(1, 1); ack = (bus_read(&bus, 0x40300, 2) >> 1) & 1;
    pins(1, 0); return ack;
}
static unsigned receive(void)
{
    unsigned n, v = 0;
    for (n = 0; n < 8; ++n) {
        pins(1, 1); v = (v << 1) | ((bus_read(&bus, 0x40300, 2) >> 1) & 1); pins(1, 0);
    }
    pins(0, 0); pins(0, 1); pins(0, 0); pins(1, 0); return v;
}
int main(void)
{
    Lcd *l = calloc(1, sizeof(*l));
    uint32_t pixels[4];
    Flash flash;
    Intc irq;
    Arm7 cpu = {0};
    Pmic p;
    uint8_t *rom = malloc(0x20000);
    CHECK(l && rom); memset(rom, 255, 0x20000);
    CHECK(lcd_attach(l, &bus, 0xc00000, 0xc80000, 16)); lcd_connect(l, pixels, 2, 2);
    CHECK(bus_read(&bus, 0xc00000, 2) == 4); cmd(0x51); CHECK(!l->enabled);
    cmd(0x2c); cmd(0x51); CHECK(bus_read(&bus, 0xc00000, 2) == 1);
    cmd(0x2a); cmd(0x50); CHECK(l->enabled); /* Parameter must not be decoded as Display Off. */
    cmd(0x43); cmd(16); cmd(17); cmd(0x42); cmd(0); cmd(1);
    bus_write(&bus, 0xc80000, 2, 0xf800); bus_write(&bus, 0xc80000, 2, 0x7e0);
    bus_write(&bus, 0xc80000, 2, 0x1f); bus_write(&bus, 0xc80000, 2, 0xffff);
    CHECK(pixels[0] == 0xff0000 && pixels[1] == 0x00ff00 && pixels[2] == 0xff && pixels[3] == 0xffffff);
    CHECK(l->x == 0 && l->y == 16); cmd(0x50); CHECK(!pixels[0]); cmd(0x51); CHECK(pixels[0] == 0xff0000);
    cmd(0x53); cmd(1); CHECK(pixels[0] == 0xffff); CHECK(bus_read(&bus, 0xc00000, 2) == 3);
    cmd(0x53); cmd(0); cmd(0x2d); CHECK(!pixels[0]); cmd(0x51); CHECK(l->standby && !pixels[0]);
    cmd(0x2c); CHECK(pixels[0] == 0xff0000); cmd(0x40); cmd(2); CHECK(bus_read(&bus, 0xc00000, 2) == 0x41);
    cmd(0x2e); CHECK(!bus.fault); CHECK(lcd_save_bmp(l, "build/lcd-fixture.bmp"));
    bus_write(&bus, 0xc80000, 1, 255); CHECK(bus.fault); bus.fault = 0;
    CHECK(bus_map(&bus, 0x1000000, 0x20000, rom, 1, 1)); CHECK(flash_attach(&flash, &bus, 0x1000000, 0x89, 0xb0));
    bus_write(&bus, 0x1000000, 2, 0x90); CHECK(bus_read(&bus, 0x1000000, 2) == 0x89 && bus_read(&bus, 0x1000002, 2) == 0xb0);
    bus_write(&bus, 0x1000010, 2, 0x40); bus_write(&bus, 0x1000010, 2, 0x1234); CHECK(bus_read(&bus, 0x1000010, 2) == 0x80);
    bus_write(&bus, 0x1000000, 2, 0xff); CHECK(bus_read(&bus, 0x1000010, 2) == 0x1234);
    bus_write(&bus, 0x1000010, 2, 0x20); bus_write(&bus, 0x1000010, 2, 0xd0);
    bus_write(&bus, 0x1000000, 2, 0xff); CHECK(bus_read(&bus, 0x1000010, 2) == 0xffff);
    CHECK(flash_geometry(&flash, 0x10000, 0x10000, 0x2000));
    CHECK(!flash_geometry(&flash, 0x10000, 0x10000, 0x2000)); /* Overlap. */
    CHECK(!flash_geometry(&flash, 0, 0x10000, 3));
    CHECK(!flash_geometry(&flash, 0x20000, 0x10000, 0x2000)); /* Outside chip. */
    CHECK(!flash_geometry(&flash, 0, 0x2000, 0x2000)); /* Splits a fallback sector. */
    memset(rom, 0x35, 0x20000);
    bus_write(&bus, 0x1012010, 2, 0x20); bus_write(&bus, 0x1012010, 2, 0xd0);
    bus_write(&bus, 0x1000000, 2, 0xff);
    CHECK(bus_read(&bus, 0x1012000, 2) == 0xffff && bus_read(&bus, 0x1013ffe, 2) == 0xffff);
    CHECK(bus_read(&bus, 0x1011ffe, 2) == 0x3535 && bus_read(&bus, 0x1014000, 2) == 0x3535);
    CHECK(bus_read(&bus, 0x1000000, 2) == 0x3535 && flash.erases == 2 && flash.last_erase == 0x1012010);
    bus_write(&bus, 0x1000010, 2, 0x20); bus_write(&bus, 0x1000010, 2, 0xd0);
    bus_write(&bus, 0x1000000, 2, 0xff);
    CHECK(bus_read(&bus, 0x100fffe, 2) == 0xffff && bus_read(&bus, 0x1010000, 2) == 0x3535);
    CHECK(intc_attach(&irq, &bus, 0x40100)); irq.period = 10; irq.periodic_bit = 1u << 26;
    intc_advance(&irq, &cpu); CHECK(!cpu.irq && !irq.pending);
    bus_write(&bus, 0x40108, 4, 1u << 26); cpu.cycles = 10; intc_advance(&irq, &cpu);
    CHECK(cpu.irq && !cpu.fiq && irq.events == 1); CHECK(bus_read(&bus, 0x40118, 4) == (1u << 26));
    bus_write(&bus, 0x4010c, 4, 1u << 26); intc_advance(&irq, &cpu); CHECK(cpu.fiq && !cpu.irq);
    bus_write(&bus, 0x40100, 4, 1u << 25); CHECK(irq.pending == (1u << 26));
    bus_write(&bus, 0x40100, 4, 1u << 26); intc_advance(&irq, &cpu); CHECK(!cpu.fiq && !irq.pending);
    CHECK(pmic_attach(&p, &bus, 0x40300, 0x90)); p.regs[8] = 0xa5; p.regs[9] = 0x3c;
    start(); CHECK(send(0x91) == 0); CHECK(send(8) == 0); CHECK(receive() == 0xa5); CHECK(receive() == 0x3c); stop();
    start(); CHECK(send(0x90) == 0); CHECK(send(10) == 0); CHECK(send(0x69) == 0); stop(); CHECK(p.regs[10] == 0x69);
    start(); CHECK(send(0x80) == 1); stop(); CHECK(!bus.fault);
    bus_destroy(&bus); free(l); printf("LCD/flash/IRQ/PMIC: %u checks passed.\n", checks); return 0;
}
