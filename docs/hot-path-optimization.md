# Interpreter/machine hot-path cleanup

This pass follows peripheral scheduling and the predecoded CPU/fetch cache.
It does not raise the emulated clock, skip instructions, batch interrupts or
change peripheral deadlines.

## Changes

- ARM cache misses now derive static timing from the already decoded execution
  class. The separate instruction-mask ladder for timing is removed. Multiply
  duration still reads its live source register before execution.
- Internal-ROM service configuration maintains an inclusive address envelope.
  Unrelated PCs skip the service scan; possible matches still use the original
  handler and validation. This is derived from model configuration, not hardcoded
  C100 ROM addresses. Adding a service updates the envelope immediately.
- Java working-buffer and battery GPIO helpers are called only at their configured
  entry addresses. Existing mode, handle and memory checks remain in the handlers.
- Fractional CPU-clock accumulation uses a carry-aware 32-bit sum on the x86 hot
  path instead of 64-bit arithmetic. Both operands are below the clock rate. If
  the unsigned addition wraps, subtracting the rate modulo 2^32 yields the same
  remainder and one nanosecond carry as the original 64-bit calculation. The
  general slow path and frequency-change table rebuild are unchanged.
- The silent abstract audio backend has no timing callback, avoiding a no-op
  indirect call per instruction. Non-null backend callbacks still run once per
  completed machine step. Yamaha and CPU-tone timing are separate and unchanged.

## Verification

- 511 architecture checks and 739,824 cached/uncached/warm CPU comparisons against
  the frozen interpreter pass after unifying ARM classification/timing.
- Machine tests now have 65,710 checks. Fractional-clock tests compare every step
  with the old 64-bit division/remainder formula across 64 rates and mixed-cycle
  Thumb instructions: 32,768 machine steps. Edge rates include 1 Hz, C100 rates,
  2^31, 4 GHz and UINT32_MAX; fractions start at rate-1 to exercise carries.
- The machine test also verifies non-null audio callbacks retain their step count
  and current cycle argument.
- Internal-ROM tests have 79 checks, including empty envelopes, inclusive bounds,
  out-of-order additions and addresses spanning zero through UINT32_MAX-1.

Benchmarks use identical pinned read-only NVM/SIM inputs and Release
`/O2 /GL /LTCG /MT` builds. Silent firmware throughput is not an audio-inclusive
performance measurement.

Two alternating XEDK1 pairs each warmed up for 250 million steps and measured
50 million steps. The baseline already includes peripheral and decode-cache work:

| Pair | Before | Hot-path cleanup | Throughput gain |
|---|---:|---:|---:|
| 1 | 2.909 s / 17.187 Msteps/s | 2.576 s / 19.411 Msteps/s | 12.9% |
| 2 | 2.894 s / 17.279 Msteps/s | 2.700 s / 18.516 Msteps/s | 7.2% |

The ratio of mean durations corresponds to about **10% additional throughput**.
All runs matched checksum `187fa131`, CPSR `00000033`, 299994293 instructions,
300000000 scheduler cycles and 28269507136 emulated nanoseconds. Host load and
scheduling affect timings; no gain is attributed to the ARM cleanup alone.

The actual pinned Yamaha hardware DLL also produced byte-identical startup/idle
WAVs before/after (4,560,044 bytes, 1,140,000 stereo frames, nonzero counter
626,180). CPU state and FIFO byte counts matched after 250 million steps. This
checks clock/audio behavior independently of the silent throughput measurement.

The native WAP Bumer download/preview regression also returned to Save? with PCM
output (nonzero counter 626,180 -> 1,841,914), no CPU halt and no Yamaha failure.
The complete Release regression build passed.
