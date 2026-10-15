# ARM7TDMI predecoded instruction cache

The core remains a C interpreter, not a JIT. Each instruction still performs
exception checks, fetches live bytes, executes once and advances its original
instruction/core-cycle counts. No firmware loop or guest instruction is skipped.

## What is cached

Each machine owns about 1 MiB of optional host-only metadata:

- Thumb: one lazy descriptor per possible 16-bit encoding (65,536 entries).
- ARM: 1,024 opcode-tagged, direct-mapped descriptors; collisions replace entries.
- A checked hint for the current instruction-fetch memory region.

Descriptors contain the instruction operation, register numbers, immediate or
relative offset, register-list counts and static cycle cost. Thumb ALU/immediate/
high-register suboperations dispatch directly to their handlers. Conditions use
NZCV truth masks, evaluated against **live** CPSR flags. Multiply duration,
register shifts, exception returns and memory values remain runtime-dependent.
Relative offsets are added to the current PC; no address-derived result is cached.

The machine owns the allocation; parked CPU contexts copy only a borrowed pointer.
Reset/loading another machine releases it. Allocation failure leaves a correct
uncached decoder. Independent machines do not share writable caches.

## Live code and bus semantics

This is content-keyed decoding, not a cache of instruction bytes. RAM stores,
DMA, host memcpy, writable aliases and flash programming change the bytes fetched
on the next instruction. Thumb selects the descriptor for those bits; ARM validates
the complete opcode tag. Old descriptors may remain but cannot execute stale code.
Changing ARM/Thumb mode selects a separate table.

The fetch-region hint checks bus identity, mapping generation and the entire
access range. Plain RAM/ROM or flash in its existing fast array mode reads current
backing bytes. Device reads, flash identify/status modes, access watches, unmapped
accesses and unsupported widths take the normal bus path. Mapping changes and bus
teardown invalidate the hint. IRQ/FIQ entry still occurs **before** any fetch.

## Checks

- 511 architecture-program checks, running with and without a cache.
- 739,824 frozen-reference comparisons: all Thumb encodings in three CPU/flag
  states plus randomized ARM, each uncached, cached and immediately repeated warm.
  Compare register banks, flags, clocks, failure state, bus faults and RAM.
- 958 targeted checks: byte/halfword/alias/host/guest/DMA code replacement, PC-relative
  reuse, flash identification/programming, changing MMIO fetch values, read watches,
  mapping replacement, pending IRQ precedence and fetch faults. All sixteen NZCV
  combinations are checked against independent logical conditions for ARM/Thumb.

Decoder-only caching initially regressed whole-phone throughput. Flattened Thumb
suboperation dispatch and the checked live-fetch hint are part of this implementation;
performance is measured for the complete path, not assumed from fewer decode checks.

Two alternating Release pairs measured 50 million machine steps after a
250-million-step warm-up, using the same pinned read-only XEDK1 snapshot:

| Pair | Peripheral-optimized baseline | Cached decoder/fetch | Throughput gain |
|---|---:|---:|---:|
| 1 | 2.939 s / 17.012 Msteps/s | 2.857 s / 17.502 Msteps/s | 2.9% |
| 2 | 2.929 s / 17.071 Msteps/s | 2.849 s / 17.552 Msteps/s | 2.8% |

All four runs matched checksum `187fa131`, CPSR `00000033`, 299994293
instructions, 300000000 scheduler cycles and 28269507136 emulated nanoseconds.
This roughly 3% gain is **silent whole-machine throughput** on this host, not
an audio-inclusive speed guarantee. The baseline already includes the earlier
peripheral optimization; decoder-only prototypes were slower and are not shipped.

The actual pinned Yamaha hardware DLL produced byte-identical startup/idle WAVs
before/after: 4,560,044 bytes / 1,140,000 stereo frames, nonzero counter 626,180.
Both 250-million-step native runs also matched CPU state and FIFO byte counts.

The cached native WAP browser downloaded and previewed Bumer, produced PCM
(nonzero counter 626,180 -> 1,841,719), and returned to Save? without CPU halt
or Yamaha failure. All Release regression checks passed.
