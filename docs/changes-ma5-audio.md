# MA-5 audio work — change record

Historical investigation notes originally comparing a working tree against
`779b800`. The original scope, check counts and candidate changes below are not
a description of the current release. Current builds must return exit code 0;
see [current audio notes](ma5-audio.md) and the README for supported behavior.
Everything is quadratic-free: every source change is either a behaviour fix or a
diagnostic gated behind an environment variable, so normal runs are unchanged.

- **Historical build:** `build.cmd release` (x86 MSVC). The original run logged
  `MA-3: 336 checks passed`; its exit-code-1 observation was a build failure,
  not a successful release-build convention.
- **Scope:** `10 files changed, 833 insertions(+), 45 deletions(-)` in tracked
  files, plus new untracked tools, a capture proxy, and six candidate patches.

---

## 1. Behaviour changes

### 1.1 Arm the 32-voice FM bank — `src/ma3.c` (`ma3_enable`)

```c
ma5_hw_register(m->hw,0x38,1,0);
```

The pinned `M5_EmuHw.dll` FM engine starts with **16** voices (its constructor
receives `1`, storing `0x10` at `engine+0x1804`; `FUN_10015670`), and key-ons for
`voice >= count` are silently dropped (`FUN_10016094`). The MA-3 reaches its
second FM bank through native voices 16..31 (`0x110..0x16F`), so those notes
vanished entirely at the default count. Register `0x38` bit 0 selects the
32-voice bank (`case 0x38 -> FUN_10016198(engine, ~v & 1)`: `0 -> 32 voices`).

This restores the missing FM / bank 2 notes. `tools/ma5fmprobe.c` demonstrated the
`+0x1804` transition from 16 to 32.

### 1.2 Delayed full-RAM packets must invalidate the ADPCM decode cache

- `src/ma3.c` (`delayed_packet`): save the packet length as `delayed_total`
  before the live count is consumed, then, for three-byte-address (full-RAM)
  packets, call `ma5_hw_ram_changed(hw, delayed_address, delayed_total)`. Those
  packets land in sample RAM exactly like the immediate path, so a refill must
  mark the decoded window stale.
- `src/ma3.h`: new `delayed_total` field.
- `src/ma5_hw.c` (`ram_changed`): in addition to overlapping the decoded input
  (`source .. source+(count+1)/2`), also mark the voice dirty when the write
  overlaps the loop-start/front region (`source .. source+4096`), so a refill that
  arrives after decode has already passed the front still triggers the wrap
  refresh. Predictor/step are still left intact (a stream is continuous across
  refills).

### 1.3 Multi-range bus watch + non-faulting peek — `src/bus.c`, `src/bus.h`

- `BUS_MAX_WATCH 8`; `Bus` gains `watch_lo[8]`, `watch_hi[8]`, `watch_count`, and
  `watch_writes_only`. The legacy `watch_start`/`watch_end` mirror the first
  parsed range so existing callers (`--mmio-trace`) are untouched.
- `bus_watch_parse(Bus *, "start,end[;start,end...]")` — returns the number of
  ranges armed, `0` on a malformed spec.
- `watch()` now skips reads when `watch_writes_only` and tests every range via
  `watch_hit()`, so the MA-3 driver block, the MMF player block and the sequencer
  block can be correlated on one tick timeline.
- `bus_peek(Bus *, address, width, *value)` — read plain mapped memory with no
  fault, no logging and no device callback. Used by the per-instruction sound
  state sampler.

### 1.4 Replay/probe correctness — `tools/ma5seqprobe.c`

Drain in 48-frame steps (`500 * 48`) instead of `50 * 480`: after a 48-frame run
a `generate(480)` would overflow the staging buffer and latch a permanent backend
failure. (Probe tool only, not part of the emulator.)

---

## 2. Frontend / CLI plumbing

### `src/gui.c`

- Diagnostics panel now reports the MA-3 commands the translation ignored:
  total count plus a per-kind list `fifo|imm ADDR xN` (up to 16 kinds).
- `MA3_ACCESS_WATCH` in `load_model()`: opens `build/gui-ram-watch.log`, calls
  `bus_watch_parse`, sets `watch_writes_only`. Comment records the hard rule:
  **do not call `setvbuf` here** — MSVC's CRT fast-fails on `_IOLBF` with size 0
  (exit `0xC0000409`) — and close the window normally so the stream flushes.

### `src/main.c`

- `MA3_ACCESS_WATCH` arm on the CLI path (writes to `build/ram-watch.log`);
  supports the same multi-range spec and writes-only mode.
- The armed ranges are re-applied after a cold reset, so a watch survives the
  firmware's reset path.
- `--mmio-trace` behaviour is unchanged.

---

## 3. Diagnostics added (all env-gated)

### `src/ma3.c`

| env var | purpose |
|---|---|
| `MA3_MASK_FIFO_EMPTY` | Report the native delayed FIFO as never empty (clear status bit 1). **Documented negative result:** the trace length and every `AUDIO` marker stayed identical, so the firmware's underrun detector is not what silences playback. Do not revisit. |
| `MA3_CURSOR_TRACE` | Log every live PCM-cursor readback (`CURSOR ch= fmt= raw= byte= biased= val=`), since `MaDevDrv_StreamUpdate` keys its refill and wrap test off that value. |
| `MA3_SNDWATCH[=path]` | Sample six sound-driver bytes (`struct+0x21/0x22/0x23/0x25/0x26/0x28` = `req pulse run irq en eos`) plus the MMF player's end-of-sequence counter once per instruction. **Documented negative result:** across 15 guest rewrites of the `0x162` mask these six bytes never move, so they are the static `MaDevDrv_InitRegisters` tables (`struct+0x21`/`+0x30`), *not* runtime sequence state. The `+0x28` "0/1/2" meaning is UNVERIFIED. |

Also in `src/ma3.c`:

- `note_unsupported()` — count and record distinct unsupported MA-3 addresses
  (immediate vs `(fifo)` delayed), instead of a bare `++m->unsupported`.
- `immediate_parameter()` — track the guest `0x162` interrupt mask
  (`irq_mask_guest`) and trace `IRQMASK value=`.
- `read_ma3()` — the `MA3_MASK_FIFO_EMPTY` experiment.
- `ma3_advance()` — call the sound-state sampler; emit `AUDIO silent|pcm at frame N`
  whenever the DLL output crosses between silence and sound, and `IRQ` on each
  accepted interrupt.
- `src/ma3.h` — `Ma3Unsupported` struct, `irq_mask_guest`, `delayed_total`,
  `unsupported_log[16]`/`unsupported_kinds`, and trace state
  (`trace_state`, `trace_frames`, `trace_nonzero`).

### `src/ma5_hw.c`

- `irq_reads` / `irq_hits` counters; `ma5_hw_irq()` counts plain reads and hits.
- `MA5HW_TRACE=<path>` — `WST %02x` / `WDT %02x` for every byte handed to the
  native DLL, plus a `GEN <frames>` marker every 64th `Hw_Generate` call. Format
  deliberately matches the capture proxy so the two feeds can be diffed.
- `MA5HW_STATE=<path>` — every 12000 frames, dump the delayed-FIFO occupancy
  (singleton `+0x6494`: capacity `+8`, read `+0xc`, write `+0x10`) and each live
  voice's decoder state, so an underrun can be told apart from idle voices.
- `hw_status()` / `hw_data()` wrappers log then forward; worker cases
  2/3/9/10/12/13/16 all go through them so nothing bypasses the trace.

---

## 4. Tests — `tests/ma3_test.c`

- Mock `ma5_hw_stats`.
- `CHECK(registers[0x38]==1)` — the DLL FM engine must be armed for 32 voices.
- New checks that a delayed full-RAM packet records `delayed_total` and notifies
  the refilled window (`changed_address`/`changed_length`), while a delayed
  *parameter* packet does not touch sample RAM.
- `unsupported_log[0]` records address `0x146`, `delayed==1`, `count==1`.

---

## 5. New tools

Built during this work; all are read-only analysis of logs/WAVs.

**New this session**

- `tools/fifo_diff.py` — structural diff of two native FIFO feeds (SMW5
  reference vs emulator): per select byte, op counts, payload bytes and payload
  sha1, plus the first divergent op. `FIFO_DIFF_OUT=<path>` writes the report as
  UTF-8. See `docs/ma5-audio.md` for its first result.

**Watch / state**

- `tools/ram_watch_report.py` — attribute `MA3_ACCESS_WATCH` writes to firmware
  functions via the `.lst`; per-address writers, timeline, largest gaps.
- `tools/ram_watch_windows.py` — per-tick-window writer attribution.
- `tools/ram_watch_gap.py` — writes immediately before/after the largest silence.
- `tools/snd_watch_report.py` — summarise `MA3_SNDWATCH`.
- `tools/state_report.py` — `MA5HW_STATE` dump next to a WAV's audible runs.
- `tools/ma3_trace_report.py` — summarise an MA-3 traffic log (`trace=1`).

**FIFO / packets**

- `tools/fifo_analyze.py`, `fifo_packets.py`, `fifo_pacing.py`, `dump_packets.py`.

**Audio / WAV**

- `tools/compare_wav.py`, `wav_envelope.py`, `wav_runs.py`,
  `find_reference.py`, `extract_ringtone.py`, `identify_mmf.py`.

**Cursor / IRQ**

- `tools/cursor_ladder.py`, `cursor_gaps.py`, `cursor_timeline.py`,
  `cursor_timeline2.py`, `irq_gaps.py`, `param_reads.py`, `window_report.py`.

**C tools**

- `tools/ma3replay.c` — offline replay of an MA-3 trace through the DLL to a WAV
  (not built by `build.cmd`; rebuild by hand after editing `ma3.c`/`ma5_hw.c`).
- `tools/ma5fmprobe.c` — proves the FM engine goes 16 -> 32 voices.

**Capture proxy — `proxy/`**

- `ma5hw_proxy.c` / `ma5hw_proxy2.c` / `ma5hw_proxy.def` + built
  `M5_EmuHw.dll`/`.exp`/`.lib`. Loads the real synth as `M5_EmuHw_real.dll`,
  exports the same `__cdecl` symbols, logs `INIT`/`TERM`/`WST`/`WDT` per FIFO byte
  and forwards unchanged. Known gap: it logs **writes only**, not reads.

**Ghidra** — `tools/ghidra/{C100Xedk1,DecompileAll}.java`.

---

## 6. Candidate patches (untracked)

`0001`..`0006` `.patch` files covering: emulation on its own thread with an audio
queue; core speedups (bus page table, flash-array fast path); optional short DLL
blocks (`audio.ini` `audio-block=96`); dropping a half-received MA-3 FIFO packet
when the guest clears mid-packet; tracing `AUDIO` silent/pcm markers; and a trace
report around the first audible sample. These are proposals, not applied.

---

## 7. Documentation

`docs/ma5-audio.md` (+446 lines) now carries the authoritative translation/IRQ
tables, the multi-range watch syntax, the RAM-watch results, the corrected
"the driver block is boot-only / the 44.68M event is a legitimate end of a
one-iteration preview" findings, and the native-FIFO-feed divergence table.
