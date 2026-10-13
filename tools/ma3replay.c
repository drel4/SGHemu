/* Replay a captured MA-3 register trace through the real translation layer and
   the pinned M5_EmuHw.dll, emitting a WAV plus a marked trace. The recorded
   timestamps are instruction ticks; ticks_hz converts them to emulated time.
   This drives the exact adapter code (src/ma3.c) with the exact guest traffic,
   without ROMs, firmware or keypad automation. */
#include "ma3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Bus bus;

int main(int argc, char **argv)
{
    Ma3 m; Intc intc; FILE *f, *trace = NULL;
    char line[512];
    unsigned long long cycle, last = 0, drain;
    unsigned ticks_hz = 11100000, off, value, sel, pc;
    char rw; int first = 1;
    const char *dll, *log, *wav;

    if (argc < 4) {
        fprintf(stderr, "Usage: ma3replay M5_EmuHw.dll trace.log out.wav [ticks_hz] [out-trace.log]\n");
        return 2;
    }
    dll = argv[1]; log = argv[2]; wav = argv[3];
    if (argc > 4) ticks_hz = (unsigned)strtoul(argv[4], NULL, 0);
    if (argc > 5) { trace = fopen(argv[5], "w"); if (!trace) { perror(argv[5]); return 1; } }

    memset(&m, 0, sizeof(m)); memset(&intc, 0, sizeof(intc));
    intc.attached = 1;
    if (!ma3_attach(&m, &bus, &intc, 0x200000, 0x800000, 1157)) { fprintf(stderr, "attach failed\n"); return 1; }
    if (!ma3_enable(&m, dll, wav, 0, ticks_hz)) { fprintf(stderr, "MA-3 hardware: %s\n", m.error); return 1; }
    ma3_set_block(&m, 96);
    m.trace = trace;

    f = fopen(log, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", log); return 1; }
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%llu %x %c %u %x sel=%x", &cycle, &pc, &rw, &off, &value, &sel) != 6) continue;
        if (cycle < last) continue;
        if (first) { m.last_tick = cycle; first = 0; } /* Start emulated time at the trace's first event. */
        bus.ticks = cycle; /* Keep the replayed trace's timestamps meaningful. */
        ma3_advance(&m, cycle);
        if (rw == 'W') bus_write(&bus, 0x200000 + off, 1, value);
        else bus_read(&bus, 0x200000 + off, 1);
        last = cycle;
    }
    fclose(f);

    drain = last + (unsigned long long)ticks_hz * 2; /* Flush ~2 s of trailing audio. */
    ma3_advance(&m, drain);

    if (*m.error) printf("MA-3 error: %s\n", m.error);
    {
        Ma5HwStats s = ma5_hw_stats(m.hw);
        printf("PCM frames=%llu nonzero=%llu peak=%u host-underruns=%llu adpcm-refreshes=%llu failed=%d\n",
               (unsigned long long)s.frames, (unsigned long long)s.nonzero, s.peak,
               (unsigned long long)s.underruns, (unsigned long long)s.adpcm_refreshes, s.failed);
    }
    printf("MA-3 reads=%llu writes=%llu fifo-bytes=%llu irq=%llu translated=%llu unsupported=%llu clears=%llu partial-drops=%llu ram-reads=%llu\n",
           (unsigned long long)m.reads, (unsigned long long)m.writes, (unsigned long long)m.fifo_bytes,
           (unsigned long long)m.irq_events, (unsigned long long)m.translated, (unsigned long long)m.unsupported,
           (unsigned long long)m.fifo_clears, (unsigned long long)m.partial_drops, (unsigned long long)m.ram_reads);
    printf("unsupported addresses:");
    { unsigned i; for (i = 0; i < m.unsupported_kinds; ++i)
        printf(" %03x%s", m.unsupported_log[i].address, m.unsupported_log[i].delayed ? "(fifo)" : "");
      printf("\n"); }
    ma5_hw_diagnostics(m.hw);
    ma3_destroy(&m);
    return 0;
}
