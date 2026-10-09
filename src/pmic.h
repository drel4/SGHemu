#ifndef SGH_PMIC_H
#define SGH_PMIC_H
#include "bus.h"
typedef struct {
    Bus *bus;
    uint32_t base;
    uint16_t lines[2];
    uint8_t regs[256];
    unsigned address, active, bits, byte, stage, index, reading, ack, selected;
    uint64_t transfers;
    int attached;
} Pmic;
int pmic_attach(Pmic *p, Bus *bus, uint32_t base, unsigned address);
#endif
