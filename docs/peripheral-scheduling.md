# Peripheral deadline fast paths

This first pass reduces host work without increasing the emulated CPU clock,
skipping guest instructions or batching interrupt delivery. The ARM interpreter
is unchanged; a predecoded instruction cache is a separate future optimization.

## Devices

- **General DMA:** cache the earliest channel deadline instead of scanning eight
  channels on every instruction. Any channel MMIO write invalidates it. A stalled
  receive channel stays due so an arriving UART byte is visible on the next step.
- **SIM:** cache the earliest timer, transmit, receive or DMA event. UART reads
  consuming a byte and register writes invalidate the deadline. Host changes to
  card presence or response length also wake it. Status interrupts still reassert
  after INTC acknowledgement even when no timed SIM event is due.
- **MA:** sample-time accounting is deferred until the next 48-kHz sample boundary.
  PCM block generation occurs on the same guest instruction as before. IRQ and LED
  polling remain per-step, including changes caused by immediate register commands.
- **Machine loop:** inactive audio outputs and DMA channels do not incur full
  device calls. CPU tone/LED observation runs only at its configured native entry
  points. Timers and IRQ/FIQ delivery retain their existing per-instruction ordering.

The lazy MA accumulator's `last_tick`/fraction reflect its last accounted sample
boundary; elapsed ticks compensate exactly when it next runs. This is internal
bookkeeping, not a change to the chip's sample rate or sequencer speed.

## Verification

`tests/peripheral_reference.inc` freezes the previous SIM/DMA advance functions
only for differential tests. Twenty thousand mixed steps compare observable state
after **every** step: RAM, card state, UART data, DMA cursors/deadlines/transfers,
interrupt latches and bus faults. Inputs include fractional register accesses,
enable/disable, RX stalls/wakeups, host card resets/removal, invalid DMA buffers
and interrupt acknowledgements: 2,340,012 checks.

MA protocol/timing tests compare old and new sample accumulation for 48 kHz,
23.4 MHz and 1-GHz timebases, irregular instruction intervals and live 16/96/480-
frame block changes. Total MA tests: 90,775 checks.

Actual-ROM probes load read-only snapshots; benchmark snapshots are copied to
ignored scratch storage first so GUI autosaves cannot change the input mid-test.
Performance measurements use Release `/O2 /GL /LTCG /MT` builds. Silent firmware
throughput and audio-inclusive behavior are separate measurements; neither
microbenchmarks nor reduced check counts alone establish whole-phone gains.

Two alternating baseline/optimized XEDK1 probes each warmed up for 250 million
steps, then measured 50 million steps from the same pinned read-only snapshot:

| Pair | Before | Deadline fast paths | Throughput gain |
|---|---:|---:|---:|
| 1 | 3.280 s / 15.245 Msteps/s | 2.989 s / 16.728 Msteps/s | 9.7% |
| 2 | 3.371 s / 14.830 Msteps/s | 3.043 s / 16.429 Msteps/s | 10.8% |

All four runs produced checksum `187fa131`, CPSR `00000033`, 299994293
instructions, 300000000 scheduler cycles and 28269507136 emulated nanoseconds.
This is approximately 10% higher **silent whole-machine throughput** on this
host, not a promise of the same improvement during live Yamaha playback.

With the actual pinned hardware DLL, 250 million startup/idle steps from the
same scratch NVM/SIM produced byte-identical 4,560,044-byte WAVs before/after
(1,140,000 stereo PCM frames, nonzero counter 626,180). CPU state and FIFO byte
counts matched as well. No host playback-speed increase is used to obtain the gain.

The optimized native browser also downloaded and previewed `Bumer-Zapravka.mmf`,
produced PCM (nonzero counter 626,180 -> 1,841,915), and returned to Save? without
a CPU halt or Yamaha failure. The previous downloaded-MMF stack fix remains intact.
