#ifndef SGH_DSP_MAILBOX_H
#define SGH_DSP_MAILBOX_H
#include "bus.h"
/* CX805 ARM/DSP exchange window. This is a protocol-level ADC subset, not a
   DSP instruction interpreter or an RF/speech implementation. */
typedef struct {
    Bus *bus;
    uint32_t base,control_address;
    uint16_t words[1024],pending[350],control,mux;
    unsigned pending_count,battery_raw,temperature_raw;
    uint64_t transactions,conversions,unsupported,malformed;
    uint8_t warned[256];
    int attached;
} DspMailbox;
int dsp_mailbox_attach(DspMailbox *d,Bus *bus,uint32_t base,uint32_t control);
void dsp_mailbox_inputs(DspMailbox *d,unsigned battery,unsigned temperature);
#endif
