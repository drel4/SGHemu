#include "sds_uart.h"
#include <string.h>
/* Generic serial-port register block, modelled from the guest's USART driver
   (`USARTOpen` 0115BFB8): `+0` control (bit0 enable, bit1 arm; zeroed while
   disabled then enabled), `+2` format, `+4` TX data, `+6` mode/status, `+8`
   control B, `+0xA` format B, `+0xC` RX data, divisor table at `+0x20..`. The
   factory probe opens it at `0x40200`; `USARTRead`/`USARTWrite` are software
   ring copies, so the bytes are moved by the general DMA controller
   (`sds_dma.c`), not by this block. The INTC bits below belong to the SDS port
   instance (`0x41200`, opened by the RF test); the factory boot never arms them.
   The cable echo is gated on the control enable bit so `USARTOpen`'s reset
   (which zeroes `+4` while disabled) does not inject a spurious byte. */
#define SDS_IRQ_TX  (1u << 14) /* SDSServiceSerialTxBuffer: load next TX byte */
#define SDS_IRQ_RX  (1u << 16) /* SDSServiceSerialRxBuffer: store received byte */
#define SDS_TX_DATA 0x04u
#define SDS_RX_DATA 0x0cu
#define SDS_WINDOW  0x30u
static int range(SdsUart *d, uint32_t off, unsigned width)
{
    if (off + width > SDS_WINDOW) {
        bus_fault_access(d->bus, 0, d->base + off, width, 0, "SDS UART out of range");
        return 0;
    }
    return 1;
}
static uint32_t sds_read(void *ctx, uint32_t off, unsigned width)
{
    SdsUart *d = ctx;
    uint32_t value = 0;
    unsigned i;
    if (!range(d, off, width)) return 0;
    if (off == SDS_RX_DATA && d->rx_ready) {
        uint32_t mask = width >= 4 ? 0xffffffffu : ((1u << (width * 8)) - 1u);
        d->rx_ready = 0;
        ++d->receives;
        return d->rx & mask & 0xffu;
    }
    for (i = 0; i < width; ++i) {
        uint16_t word = d->regs[(off + i) >> 1];
        value |= (uint32_t)((word >> (8 * ((off + i) & 1u))) & 0xffu) << (8 * i);
    }
    return value;
}
static void sds_write(void *ctx, uint32_t off, unsigned width, uint32_t value)
{
    SdsUart *d = ctx;
    unsigned i;
    if (!range(d, off, width)) return;
    for (i = 0; i < width; ++i) {
        uint16_t *word = &d->regs[(off + i) >> 1];
        unsigned shift = 8 * ((off + i) & 1u);
        *word = (uint16_t)((*word & ~(0xffu << shift)) | (((value >> (8 * i)) & 0xffu) << shift));
    }
    if (off == SDS_TX_DATA && (d->regs[0] & 1u)) {
        /* A byte was loaded into the holding register of an enabled port: it is
           transmitted (with a virtual service cable, looped straight back into the
           receiver) and the register becomes free again, which is the TX service
           condition. The DMA controller loads the register with a byte write, the
           guest's serial services with a halfword write. Open time zeroes the
           register while the port is still disabled, so the enable bit gates it. */
        ++d->transmits;
        if (d->cable) {
            d->rx = value & 0xffu;
            d->rx_ready = 1;
            d->intc->pending |= SDS_IRQ_RX;
        }
        d->intc->pending |= SDS_IRQ_TX;
    }
    if ((off == 0x00u || off == 0x08u) && (value & 3u)) {
        /* Control enable/arm: the TX register is empty, so the firmware TX
           service may load a queued byte. */
        d->intc->pending |= SDS_IRQ_TX;
    }
}
int sds_uart_attach(SdsUart *d, Bus *bus, Intc *intc, uint32_t base, int cable)
{
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->intc = intc; d->base = base; d->cable = cable;
    if (!bus_device(bus, base, SDS_WINDOW, sds_read, sds_write, d)) return 0;
    d->attached = 1; return 1;
}
int sds_uart_rx_ready(const SdsUart *d)
{
    return d && d->rx_ready;
}
