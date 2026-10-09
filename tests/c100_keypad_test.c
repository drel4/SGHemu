#include "machine.h"
#include "nvram.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"C100 keypad line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned press(Machine *m, unsigned key)
{
    unsigned phase, i, events = 0;
    for (phase = 0; phase < 2; ++phase) {
        machine_key(m,key,phase == 0);
        for (i = 0; i < 2000000; ++i) {
            /* XEDK1 ker_event: verify guest decoding, not injected UI actions. */
            if (m->cpu.r[15] == 0x011390c8) printf("host=%u scan=%02x release=%u\n",key,m->cpu.r[1],m->cpu.r[0]);
            if (m->cpu.r[15] == 0x0112d398) {
                printf("host=%u guest=%02x\n",key,m->cpu.r[0]); ++events;
            }
            if (!machine_step(m,1)) { fprintf(stderr,"Guest fault\n"); exit(1); }
        }
    }
    return events;
}
int main(int argc, char **argv)
{
    static const unsigned expected[22] = {
        0x47,0x24,0x48,0x44,0x47,0x45,0x27,0x25,0x28,
        '1','2','3','4','5','6','7','8','9','*','0','#',0x26
    };
    Machine *m = calloc(1,sizeof(*m));
    unsigned key;
    uint32_t table;
    if (argc != 2 || !m) { fprintf(stderr,"Usage: c100_keypad_test COMPLETED-XEDK1.snv\n"); free(m); return 2; }
    CHECK(machine_load(m,"models/c100/c100xedk1-bringup.cfg",NULL,NULL));
    CHECK(nvram_load(m,argv[1])); /* Read-only fixture: never changes the user's save. */
    m->sim.present=0; /* Keep the no-SIM UI fixture used for these input assertions. */
    while (m->cpu.instructions < 30000000) if (!machine_step(m,1)) { fprintf(stderr,"Guest boot fault\n"); return 1; }
    table = bus_read(&m->bus,0x004065f8,4);
    for (key = 0; key < 22; ++key) {
        unsigned raw = ((unsigned)m->keypad.row[key] << 5) + m->keypad.column[key] + 1, entry;
        CHECK(m->keypad.mapped & (1u << key));
        for (entry = 0; entry < 32; ++entry) {
            unsigned code = bus_read(&m->bus,table + entry * 8,1);
            if (!code || code == raw) break;
        }
        CHECK(entry < 32 && bus_read(&m->bus,table + entry * 8,1) == raw);
        CHECK(bus_read(&m->bus,table + entry * 8 + 5,1) == expected[key]);
    }
    CHECK(press(m,9)); CHECK(press(m,10)); /* Dial 12. */
    CHECK(press(m,21)); /* Real kid_HandleClear: delete the last digit. */
    CHECK(lcd_save_bmp(&m->lcd,"build/keypad-clear.bmp"));
    CHECK(press(m,8)); /* End should leave the dial input, not activate a soft key. */
    CHECK(lcd_save_bmp(&m->lcd,"build/keypad-end.bmp"));
    CHECK(press(m,0)); CHECK(lcd_save_bmp(&m->lcd,"build/keypad-soft-left.bmp"));
    CHECK(press(m,2)); CHECK(lcd_save_bmp(&m->lcd,"build/keypad-soft-right.bmp"));
    printf("C100 guest keypad: mapping and live event checks passed (%u checks).\n",checks);
    machine_destroy(m); free(m); return 0;
}
