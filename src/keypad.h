#ifndef SGH_KEYPAD_H
#define SGH_KEYPAD_H
#include "bus.h"
typedef struct {
    Bus *bus;
    uint32_t base, held, mapped;
    uint16_t control, drive;
    uint8_t row[32], column[32], rows, columns;
    uint64_t reads, writes;
    int attached;
} Keypad;
int keypad_attach(Keypad *d, Bus *bus, uint32_t base, unsigned rows, unsigned columns);
int keypad_map(Keypad *d, unsigned key, unsigned row, unsigned column);
void keypad_key(Keypad *d, unsigned key, int down);
#endif
