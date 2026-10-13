#include "sds_dma.h"
#include <string.h>
/* Register offsets inside one 0x40-byte channel block. */
#define DMA_CONTROL   0x00u
#define DMA_STATUS    0x02u
#define DMA_SRC       0x04u
#define DMA_DST       0x08u
#define DMA_THRESHOLD 0x0cu
#define DMA_END       0x10u
#define DMA_CUR_SRC   0x14u
#define DMA_CUR_DST   0x18u
#define DMA_USED      0x1cu
/* Default startup and per-byte pacing. The driver enables a channel from
   USARTOpen and only then queues the first byte, so the first transfer must wait
   long enough for that write to land; the per-byte gap keeps a free-running
   channel from outrunning the driver's ring producer. Both are model-tunable
   through the sds_dma config key. The defaults are the values measured to work
   on the factory loopback (enable->poll window is under 0x300 instructions). */
#define DMA_START_DELAY 256u
#define DMA_BYTE_TIME   128u
static uint32_t field(const SdsDmaChannel *c, uint32_t off)
{
    switch (off & ~3u) {
    case DMA_CONTROL:   return (uint32_t)c->control | ((uint32_t)c->status << 16);
    case DMA_SRC:       return c->src;
    case DMA_DST:       return c->dst;
    case DMA_THRESHOLD: return c->threshold;
    case DMA_END:       return c->end;
    case DMA_CUR_SRC:   return c->cur_src;
    case DMA_CUR_DST:   return c->cur_dst;
    default:            return 0;
    }
}
static uint32_t block_read(void *ctx, uint32_t off, unsigned width)
{
    SdsDmaChannel *c = ctx;
    uint32_t word, mask;
    if (width != 1 && width != 2 && width != 4) return 0;
    if (off + width > DMA_USED) return 0;
    word = field(c, off);
    mask = width >= 4 ? 0xffffffffu : ((1u << (width * 8)) - 1u);
    return (word >> (8 * (off & 3u))) & mask;
}
static void block_write(void *ctx, uint32_t off, unsigned width, uint32_t value)
{
    SdsDmaChannel *c = ctx;
    uint32_t word, mask;
    unsigned shift;
    if (width != 1 && width != 2 && width != 4) return;
    if (off + width > DMA_USED) return;
    shift = 8 * (off & 3u);
    mask = width >= 4 ? 0xffffffffu : ((1u << (width * 8)) - 1u);
    word = field(c, off);
    word = (word & ~(mask << shift)) | ((value & mask) << shift);
    switch (off & ~3u) {
    case DMA_CONTROL: {
        int was = c->running;
        c->control = (uint16_t)word;
        c->status = (uint16_t)(word >> 16);
        c->running = (c->control & 1u) != 0;
        if (c->running && !was) { c->cur_src = c->src; c->cur_dst = c->dst; c->started = 0; }
        else if (!c->running) { c->started = 0; }
        break;
    }
    case DMA_SRC:       c->src = word; break;
    case DMA_DST:       c->dst = word; break;
    case DMA_THRESHOLD: c->threshold = word; break;
    case DMA_END:       c->end = word; break;
    default: break;
    }
}
int sds_dma_attach(SdsDma *d, Bus *bus, SdsUart *serial, uint32_t base, unsigned channels,
    uint32_t start_delay, uint32_t byte_time)
{
    unsigned i;
    if (!bus || !channels || channels > SDS_DMA_MAX_CHANNELS) return 0;
    memset(d, 0, sizeof(*d));
    d->bus = bus;
    d->serial = serial;
    d->serial_rx = (serial && serial->attached) ? serial->base + 0x0cu : 0;
    d->channels = channels;
    d->start_delay = start_delay ? start_delay : DMA_START_DELAY;
    d->byte_time = byte_time ? byte_time : DMA_BYTE_TIME;
    /* A channel region an existing device already owns (the SIM controller sits
       at 0x40520) is left to that owner rather than being an error. */
    for (i = 0; i < channels; ++i) {
        SdsDmaChannel *c = &d->channel[i];
        c->bus = bus;
        c->base = base + i * SDS_DMA_CHANNEL_STRIDE;
        if (bus_device(bus, c->base, SDS_DMA_CHANNEL_STRIDE, block_read, block_write, c)) ++d->registered;
    }
    d->attached = 1;
    return 1;
}
static void advance_channel(SdsDma *d, SdsDmaChannel *c, uint64_t now)
{
    uint32_t dir = c->control & 0x18u;
    if (!c->started) { c->started = 1; c->next_due = now + d->start_delay; return; }
    if (now < c->next_due) return;
    if (dir == 0x10u || dir == 0x18u) {
        /* Memory -> device: consume the producer's byte and hand it to the port. */
        uint32_t start = c->src, pos = c->cur_src;
        uint8_t byte;
        if (pos < start || pos > c->end) pos = start;
        byte = (uint8_t)bus_read(d->bus, pos, 1);
        bus_write(d->bus, c->dst, 1, byte);
        if (++pos > c->end) pos = start;
        c->cur_src = pos;
        ++c->bytes;
    } else if (dir == 0x08u) {
        /* Device -> memory: only when the port actually latched a receive byte. */
        uint32_t start = c->dst, pos = c->cur_dst;
        uint8_t byte;
        if (d->serial && c->src == d->serial_rx && !sds_uart_rx_ready(d->serial)) return;
        if (pos < start || pos > c->end) pos = start;
        byte = (uint8_t)bus_read(d->bus, c->src, 1);
        bus_write(d->bus, pos, 1, byte);
        if (++pos > c->end) pos = start;
        c->cur_dst = pos;
        ++c->bytes;
    } else {
        return;
    }
    c->next_due = now + d->byte_time;
    ++d->transfers;
}
void sds_dma_advance(SdsDma *d, uint64_t now)
{
    unsigned i;
    if (!d->attached) return;
    for (i = 0; i < d->channels; ++i) {
        SdsDmaChannel *c = &d->channel[i];
        if (!c->running) continue;
        advance_channel(d, c, now);
    }
}
