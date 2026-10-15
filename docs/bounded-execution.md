# Bounded execution and data-bus fast paths

## What changes (and what does not)

The GUI runs ordinary execution in batches of at most 64 machine steps.
`machine_step` remains the reference/debug path. `machine_run` still executes
every guest instruction and updates the core-cycle clock after each one.
There is **no idle-loop detection, instruction skipping, CPU overclock or JIT**.
The ARM7TDMI interpreter remains plain C17.

Between device deadlines the runner avoids repeated peripheral/HLE dispatch.
It returns to the reference path before an emulated-time/cycle deadline or
firmware-service entry. A callback access increments the bus device epoch,
ending the fast path after that instruction. This includes reads with side
effects and flash/clock/interrupt writes, not just ordinary MMIO stores.

Both clock domains are checked before each instruction. An instruction may
cross a deadline; the next step services it before executing another instruction,
exactly as the reference loop does. IRQ/FIQ entry is still checked by the CPU
on every instruction, including changes to CPSR masks. Masked periodic timer
bookkeeping is preserved, not just its eventual interrupt.

Native firmware entry points come from the live model bindings. A small Bloom
filter permits false positives (an unnecessary reference step), never a missed
configured entry. SIM/DMA, low-power timers, charger completion, MA sample/Water
deadlines and independent CPU-tone output all contribute bounds. Yamaha worker
commands are synchronous: between completed commands/generation calls no DLL
sequencer work runs; MMIO and sample boundaries therefore retain IRQ/LED polling.

Guest provisioning, content imports, legacy battery injection, unknown timed
audio backends, and access-watch logging retain the conservative step path.
Frontend text/ghosting/profile sampling boundaries remain bounded separately.
Host input still uses short GUI slices; execution is not one enormous CPU lock.

CPU data reads/writes can bypass general bus dispatch only for an entire cached
memory page with a valid access width and no cross-page access. RAM aliases use
the same live backing bytes. Diagnostic watches, partial pages, device reads,
readonly writes and all write callbacks retain normal dispatch. Flash array
reads can be fast, but flash commands/programming never become RAM stores.

## Verification

- 40,019 data-bus checks, including unaligned/cross-page accesses, aliases,
  readonly/unmapped/invalid accesses, fast flash reads and write callbacks,
  and diagnostic read/write traces.
- Randomized batch-versus-step comparisons across 4,000 boundaries: CPU banks,
  flags, IRQ/FIQ, gate/mask changes, clocks/fractions, RAM, SIM, DMA, low-power
  timers, charger state, HLE memcpy, tone observers, faults and strict stopping.
- The complete Release CPU/differential/peripheral/network/audio/import suite.
- Native charging runs on XEDA2/XEDD1/XEDK1 reach the hatched chick at 100%; a
  25% control remains in charging mode, with the preceding LCD-write counts.
- Native XEDK1 WAP fetch, session exit/Resume and distinct second URL, with
  Yamaha enabled, complete without CPU/DLL failure.
- Single-step and batched startup/ringtone-selection runs produced identical
  9,899,564-byte PCM WAVs: 2,474,880 stereo frames, 3,209,620 nonzero samples,
  FIFO 114,529 bytes, final clock 51,565,774,059 ns. Inputs/captures stay private.

## Measurements

Same read-only XEDK1 snapshot and hardware profile; Release x86, 250 million
warmup steps followed by 50 million measured steps. Three alternating rounds:

| Variant | Median seconds | Median Msteps/s |
| --- | ---: | ---: |
| v0.3.1 reference build | 2.852 | 17.534 |
| Data fast paths + bounded execution | 1.519 | 32.923 |
| Additional block-decoder prototype | 1.608 | 31.091 |
| Bounded execution + trained benchmark PGO | 1.234 | 40.514 |

All runs matched CPU/LCD checksum `cd8f44b3`, CPSR `40000033`, instruction
count 299,994,291, scheduler steps 300,000,000 and clock 28,270,497,948 ns.
The standalone runner used batches of 256; the GUI uses 64 for responsiveness.
These are **silent throughput** measurements, not live-audio or game speed
promises. PGO numbers describe the separately trained benchmark executable,
not an assumed identical gain from the GUI profile.

The added block cache was slower in this workload, so it was **not shipped**.
Existing live-opcode decoding/fetch validation remains intact. The actual GUI
was separately PGO-trained through boot, menus/ringtone selection, audio startup,
appearance and pause/resume using isolated saved state. An optimized-GUI check
with Yamaha active reported approximately 99–101% real-time speed; sound-card
pacing waits are expected and are not extra CPU cost.

See [PGO build workflow](profile-guided-build.md). Ordinary Release builds need
no training data and still include the data/batching improvements.
