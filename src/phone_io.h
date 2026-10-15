#ifndef SGH_PHONE_IO_H
#define SGH_PHONE_IO_H
#include "arm7.h"
typedef struct PhoneOutput PhoneOutput;
typedef struct {
    uint32_t tone_entry,tone_stop,tone_table,gpio_entry;
    unsigned led_pins[3],led;
    double phase[2],step[2],amplitude[2],render_amplitude[2];
    uint64_t led_tick,last_ns,fraction,frames,nonzero,tone_changes,led_changes;
    PhoneOutput *output;
    int configured,mixed,hardware_led;
} PhoneIo;
/* Observe verified guest driver entries without replacing their execution. */
void phone_io_observe(PhoneIo *io,Arm7 *cpu);
/* Hardware data-port callback; pin numbers are board wiring, not guest PCs. */
void phone_io_gpio_write(void *context,unsigned mask,unsigned value,uint64_t ticks);
int phone_io_open(PhoneIo *io,int speakers,const char *wav);
void phone_io_pause(PhoneIo *io,int paused);
void phone_io_mix(void *context,int16_t *left,int16_t *right,unsigned frames);
void phone_io_advance(PhoneIo *io,uint64_t ns);
void phone_io_close(PhoneIo *io);
#endif
