/* Frozen pre-deadline timing model; test-only, never linked into SGHemu. */
static void reference_channel(SdsDma *d, SdsDmaChannel *c, uint64_t now)
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
static void reference_dma(SdsDma *d, uint64_t now)
{
    unsigned i;
    if (!d->attached) return;
    for (i = 0; i < d->channels; ++i) {
        SdsDmaChannel *c = &d->channel[i];
        if (!c->running) continue;
        reference_channel(d, c, now);
    }
}

static void reference_sim(Sim *s,uint64_t ticks)
{
    if (!s->attached) return;
    if (s->timer_due && ticks>=s->timer_due) { s->timer_due=0;s->intc->pending|=1; }
    if (s->tx_due && ticks>=s->tx_due) { s->tx_due=0;s->regs[2]|=0x10;if(s->present)sim_card_send(&s->card,s->tx); }
    if (s->present && s->card.response_size && ticks>=s->rx_due && (s->regs[0]&0xb)==0xb) s->regs[2]|=8;
    if (s->present && (s->dma[0]&1) && ticks>=s->dma_due) {
        uint32_t ptr,mode=s->dma[0]&0x18;
        unsigned i; Region *r=NULL; uint8_t byte;
        ptr=mode==0x10?s->dma[5]:s->dma[6];
        for(i=0;i<s->bus->count;++i) if(s->bus->regions[i].data && ptr>=s->bus->regions[i].base &&
            (uint64_t)ptr<(uint64_t)s->bus->regions[i].base+s->bus->regions[i].size) {r=&s->bus->regions[i];break;}
        if(!r || (mode!=0x10 && r->readonly) || (mode!=8 && mode!=0x10)) {
            bus_fault_access(s->bus,mode!=0x10,ptr,1,0,"invalid SIM DMA buffer/mode");s->dma[0]&=~1u;
        } else if(mode==0x10 || (s->regs[2]&8)) {
            if(mode==0x10) {byte=(uint8_t)bus_read(s->bus,ptr,1);sim_card_send(&s->card,byte);++s->transmitted;++s->dma[5];}
            else {sim_card_receive(&s->card,&byte);bus_write(s->bus,ptr,1,byte);++s->received;++s->dma[6];s->regs[2]&=0xfff7u;}
            ++s->dma_bytes;s->dma_due=ticks+1000;s->rx_due=ticks+1000;
            if(ptr==s->dma[3]) {s->intc->pending|=0x400;s->dma[0]&=~1u;}
            if(mode==0x10 && ptr==s->dma[3])s->regs[2]|=0x10;
        }
    }
    if (s->regs[2]&s->regs[3]&0x18) s->intc->pending|=8;
}
