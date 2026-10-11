#ifndef SGH_MACHINE_H
#define SGH_MACHINE_H
#include "arm7.h"
#include "audio.h"
#include "flash.h"
#include "lcd.h"
#include "intc.h"
#include "pmic.h"
#include "irom.h"
#include "keypad.h"
#include "sim.h"
#include "ma3.h"
#define SGH_MAX_IDENTITIES 16
#define SGH_MAX_KEYS 22
typedef struct { Bus *bus; uint32_t address; uint16_t value; } Identity16;
typedef struct { Bus *bus; uint32_t address, value; } Register32;
typedef struct { uint32_t base; size_t size; char path[1024]; } RomImage;
typedef struct {
    Bus *bus;
    uint32_t base, reference_hz, arm_hz, dsp_hz;
    uint16_t registers[3], encoding[8];
    uint32_t arm_rates[8], dsp_rates[8];
    unsigned count;
    uint64_t nanoseconds, fraction, clock_cycles;
    /* Per-step conversion cache: delta cycles -> whole ns and remainder at
       table_hz, so the hot path needs no 64-bit division. */
    uint64_t step_ns[33]; uint32_t step_rem[33], table_hz;
    int attached, unknown;
} PhoneClock;
typedef struct {
    Bus bus;
    Arm7 cpu;
    Audio audio;
    uint32_t reset;
    char name[160], config_path[1024], error[256];
    uint32_t *pixels; /* top-down XRGB8888, supplied by the future LCD device */
    unsigned width, height;
    uint32_t keys;
    uint8_t *unplaced_rom; /* Retained image; never exposed at a guessed guest address. */
    size_t unplaced_size;
    char unplaced_path[1024];
    Identity16 identities[SGH_MAX_IDENTITIES];
    unsigned identity_count;
    Register32 registers[SGH_MAX_IDENTITIES];
    unsigned register_count;
    Flash flashes[4];
    unsigned flash_count;
    RomImage roms[4];
    unsigned rom_count;
    uint32_t nvram_base, nvram_size;
    uint8_t *nvram_data; /* Configured writable flash tail, not an invented filesystem. */
    uint32_t nv_trigger, nv_format, nv_marker; /* Verified guest factory-service entries. */
    Lcd lcd;
    Intc intc;
    Pmic pmic;
    Irom irom;
    Keypad keypad;
    Sim sim;
    Ma3 ma3;
    PhoneClock clock;
    int periodic_time;
    uint32_t fs_header, fs_table, fs_capacity, fs_stride;
    uint32_t irq_gate_address, irq_gate_mask;
    Register32 *irq_gate_register;
    int loaded, bringup;
} Machine;
/* Initialize to zero before first use. Loading tears down the previous machine. */
int machine_load(Machine *m, const char *config_path, const char *rom_override, FILE *log);
int machine_load_pair(Machine *m, const char *config_path, const char *rom_override,
    const char *second_override, FILE *log);
void machine_destroy(Machine *m);
int machine_step(Machine *m, int continue_unmapped);
void machine_key(Machine *m, unsigned key, int down);
void machine_pattern(Machine *m, int enabled);
#endif
