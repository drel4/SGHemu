#ifndef SGH_SDS_DMA_H
#define SGH_SDS_DMA_H
#include "bus.h"
#include "sds_uart.h"
/* Observed CX805 general DMA controller (SetDMAController 0x0115DA10).
   Eight channels at 0x40420 + n*0x40, each register block:
     +0x00 u16 control  (bit0 enable, bit2 irq enable, bits3:4 direction
                         0x8 = device->memory, 0x10 = memory->device)
     +0x02 u16 status   (DMABufferStatus: low 6 bits)
     +0x04 u32 source start
     +0x08 u32 destination start
     +0x0C u32 interrupt threshold (SetDMAThreshold)
     +0x10 u32 end of the memory walk (wrap point)
     +0x14 u32 current source pointer (CurrentDMAPtr dir 0)
     +0x18 u32 current destination pointer (CurrentDMAPtr dir 1)
   The U(A)RT byte stream only moves while a channel is enabled, which is why a
   loopback probe needs this block. */
#define SDS_DMA_MAX_CHANNELS 8
#define SDS_DMA_CHANNEL_STRIDE 0x40u
typedef struct {
    Bus *bus;
    uint32_t base;
    int running, started;
    uint16_t control, status;
    uint32_t src, dst, threshold, end;
    uint32_t cur_src, cur_dst;
    uint64_t next_due, bytes;
} SdsDmaChannel;
typedef struct {
    Bus *bus;
    SdsUart *serial;      /* port whose RX register paces a device->memory channel */
    uint32_t serial_rx;   /* its data register address, 0 when no port is attached */
    SdsDmaChannel channel[SDS_DMA_MAX_CHANNELS];
    unsigned channels, registered;
    uint32_t start_delay, byte_time; /* programmable pacing, CPU cycles */
    uint64_t transfers;
    int attached;
} SdsDma;
/* The port is optional: a controller with no port still models the register
   block, it just cannot tell when a receive byte is waiting. start_delay/byte_time
   of 0 select the default pacing. */
int sds_dma_attach(SdsDma *d, Bus *bus, SdsUart *serial, uint32_t base, unsigned channels,
    uint32_t start_delay, uint32_t byte_time);
/* Move at most one byte per enabled channel. Called once per instruction with
   the CPU cycle count, so a memory->device channel cannot outrun the driver's
   ring producer. */
void sds_dma_advance(SdsDma *d, uint64_t now);
#endif
