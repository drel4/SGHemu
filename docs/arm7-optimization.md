# ARM7TDMI interpreter optimization

The CPU remains a plain-C ARMv4T interpreter. There is no JIT, guessed clock
increase, skipped firmware idle loop or batched interrupt delivery.

## Changes

- Thumb dispatch uses the instruction format's high nibble, rather than walking
  every preceding instruction family's mask checks.
- Each Thumb handler supplies its core-cycle count alongside execution. This
  removes a second decode for timing; PUSH/POP and multiple-transfer counts are
  reused rather than counted again.
- Thumb conditional branches evaluate the condition once for both timing and
  execution. Multiply timing still uses the source register before multiplication.
- ARM conditions are evaluated once before dispatch, with an immediate path for
  AL. Failed conditions still consume one cycle; ARMv4T NV/reserved encodings keep
  the previous reserved-instruction failure, not an accidental silent skip.
- Instruction fetching remains through the live bus: self-modifying RAM code,
  flash modes, MMIO callbacks, bus faults and access watches are not bypassed.
- IRQ/FIQ entry still precedes instruction fetching and decoding on each step.

The shared CPU core does not contain any C100-specific address or model rule.

The next optimization pass targets [peripheral deadline checks](peripheral-scheduling.md)
rather than further CPU decoding changes. The interpreter is unchanged by that pass.

A subsequent pass adds the [ARM/Thumb predecode cache](arm7-decode-cache.md),
including checked live instruction fetching and cache-hit regression coverage.

The [following hot-path pass](hot-path-optimization.md) completes unified ARM
classification/timing and reduces unrelated HLE checks and fractional-clock work.

## Correctness checks

`tests/arm7_reference.c` is a frozen pre-optimization decoder compiled only into
verification tools, not the emulator. `tests/cpu_diff_test.c` compares all 65536
Thumb encodings in three CPU/data/flag states and 50000 randomized ARM encodings:
**246608 cases**. It compares register banks, CPSR/SPSRs, PC, instruction/scheduler
and core-cycle counts, failure state, bus faults and all test RAM bytes.

Equivalence with a previous decoder is not proof of the architecture. The
separate 255-check instruction-program suite remains the primary flag, memory,
interworking, bank and IRQ/FIQ regression suite. Both suites run in `build.cmd`.

## Benchmarks

`build/cpubench.exe` compares the frozen and optimized interpreters on the same
six-instruction RAM-based loops, including loads, stores, ALU work and branches.
Each run executes 50 million instructions, without GUI, peripherals or audio.
A Release `/O2 /GL /LTCG /MT` sample produced:

| CPU workload | Reference MIPS | Optimized MIPS | Throughput gain |
|---|---:|---:|---:|
| Thumb | 48.088 | 57.076 | 18.7% |
| ARM | 39.494 | 45.690 | 15.7% |

Reference and optimized states match, and each loop reports exactly 91666667
core cycles. These are isolated microbenchmarks, not whole-phone speed estimates.
Host load and scheduling affect measured throughput.

`build/perfbench.exe MODEL.cfg READ_ONLY.snv` separately measures the CPU and
peripherals in an actual firmware run after a 250-million-step warm-up. Input
storage is read-only. This checks practical benefit and timing/state equivalence
without distributing firmware or user snapshots.

A paired XEDK1 firmware run measured 50 million machine steps after the same
warm-up: **3.552 s / 14.075 Msteps/s** before, **3.406 s / 14.681 Msteps/s** after,
approximately **4.3% greater whole-machine throughput**. Both produced checksum
`a2f5c9a5`, CPSR `60000033`, 299994267 instructions, 300000000 scheduler cycles
and 28273106195 emulated nanoseconds. The smaller whole-machine gain is expected:
peripheral and scheduler work remain in the machine loop. No audio-inclusive
performance gain is claimed from this silent benchmark.
