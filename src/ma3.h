#ifndef SGH_MA3_H
#define SGH_MA3_H
#include "intc.h"
#include "ma5_hw.h"
typedef struct {
    Bus *bus; Intc *intc; Ma5Hw *hw;
    uint32_t base, irq_mask, ticks_hz, sequence_quantum_ns;
    uint64_t last_tick, fractional, pending_frames, reads, writes, fifo_bytes, irq_events;
    unsigned selector; int attached;
    uint8_t parameter_ram[0x400];
    uint32_t packet_address, packet_length;
    unsigned packet_phase, address_bytes, length_bytes, response, response_mask;
    uint64_t ram_reads;
    unsigned response_selector, bank, delayed_length, delayed_phase, delay_end, address_end, address_count;
    unsigned delayed_address, delayed_count, delayed_count_bytes;
    uint8_t delayed[1024];
    uint64_t translated, unsupported;
    FILE *trace; char error[256];
} Ma3;
int ma3_attach(Ma3 *m,Bus *bus,Intc *intc,uint32_t base,uint32_t irq_mask,uint32_t sequence_quantum_ns);
int ma3_enable(Ma3 *m,const char *dll,const char *wav,int speakers,uint32_t ticks_hz);
void ma3_advance(Ma3 *m,uint64_t ticks);
void ma3_destroy(Ma3 *m);
#endif
