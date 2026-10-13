#ifndef SGH_BATTERY_H
#define SGH_BATTERY_H
#include "arm7.h"
/* Model-declared guest interfaces. Native firmware handles UI and charging. */
typedef struct {
    uint32_t ready,alloc,report,detect,gpio,pin;
    unsigned percent,charging,voltage;
    int enabled,level_pending,cable_pending,stage;
    Arm7 caller;
} Battery;
void battery_set(Battery *b,unsigned percent,int charging);
int battery_gpio(Battery *b,Arm7 *cpu);
int battery_step(Battery *b,Arm7 *cpu);
#endif
