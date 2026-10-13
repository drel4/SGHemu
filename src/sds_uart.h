#ifndef SGH_SDS_UART_H
#define SGH_SDS_UART_H
#include "bus.h"
#include "intc.h"
/* Observed CX805 serial port register block (base is model-configured):
   control +0, format +2, TX data +4, status +6, control B +8, format B +0A,
   RX data +0C, divisor table +0x20..+0x2E.
   Two firmware paths drive it. The SDS driver runs it from its serial interrupt
   services, which the firmware ISR dispatches on interrupt bits 14 (TX) and 16
   (RX). The USART driver instead hands the byte to the general DMA controller,
   which moves to/from +4/+0C while its channel is enabled. The module models
   both: it raises the two interrupt lines and it latches a received byte for the
   DMA's device->memory channel to pick up. */
typedef struct {
    Bus *bus;
    Intc *intc;
    uint32_t base;
    uint16_t regs[0x18]; /* 0x41200..0x4122E, halfword indexed */
    int cable;           /* virtual service cable attached: loopback the byte */
    unsigned rx;         /* halfword last written to the TX data register */
    int rx_ready;
    uint64_t transmits, receives;
    int attached;
} SdsUart;
int sds_uart_attach(SdsUart *d, Bus *bus, Intc *intc, uint32_t base, int cable);
/* Non-destructive receive probe for the DMA controller: is a byte latched in the
   RX data register and waiting to be consumed? */
int sds_uart_rx_ready(const SdsUart *d);
#endif
