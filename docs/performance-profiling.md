# Performance profiling

Enable **File -> Logging**, reproduce the slow operation for 15-30 seconds,
then turn logging off. Share `build/gui-profile.log` for performance diagnosis;
MMIO and MA traffic remain in their existing separate logs. All three append
across toggles and resets. The existing saved logging preference controls them.
Profiling does not change CPU clocks, audio timing, or firmware settings.

Reports appear approximately once per wall-clock second, with a final report
when logging is disabled, the model resets, or the emulator closes. Each session
records build mode, model, configured ARM frequency and audio block size.

| Field | Meaning |
|---|---|
| `arm_hz` | Current interpreted ARM clock setting, including runtime divider changes |
| `mips`, `instructions` | Interpreted guest instructions per wall second and interval count |
| `emu_s`, `speed_pct` | Emulated clock progression; 100% means one emulated second per wall second |
| `cpu_ms` | CPU interpreter thread kernel + user CPU time; excludes time blocked waiting |
| `slice_ms`, `max_slice_ms` | Total and longest slice wall time, including synchronous audio waits |
| `gui_wait_ms`, `max_gui_wait_ms` | GUI command/input waits to acquire the emulation mutex |
| `timer_skips` | Refresh timers that skipped a busy mutex instead of blocking the UI |
| `audio_commands` | Synchronous Yamaha worker requests |
| `audio_generate_wait_ms` | Time waiting for render requests, including output pacing |
| `audio_other_wait_ms` | Time waiting for other Yamaha worker requests, including scheduling |
| `net_locks`, `net_lock_wait_ms` | CPU-side packet queue lock acquisitions and acquisition time |
| `tx`, `rx` | Packets submitted/received by the emulated transport in the interval |
| `guest_pc_samples` | Up to eight most frequently sampled guest program-counter addresses |
| `pc_samples`, `pc_dropped` | Total samples and samples dropped when the bounded table is full |

The emulated-time speed estimate depends on the model's clock interpretation,
which is not a calibrated hardware measurement. These are overlapping metrics:
**do not sum slice wall time, CPU time and audio waits**. A high render wait with
speed near 100% can be normal audio pacing. High CPU time with low speed suggests
interpreter/peripheral work; frequent other audio waits can indicate excessive
synchronous DLL exchanges. Compare queue-lock timing before blaming networking.

PC sampling runs at slice start and every 1024 slice iterations thereafter, with
4096 bounded address slots per interval and at most eight lookup probes per
sample. The bound prevents the profiler itself doing long full-table scans. Counts are approximate guest hotspots,
not cycle counts or host-function samples. Locate those addresses in Ghidra to
inspect native idle loops or unusually busy firmware routines.

When disabled, no profiling timers, CPU-time queries or samples are collected.
There are only cheap enable checks on normal execution paths. Existing traffic
logging and the enabled profiler do add overhead: capture representative short
runs, and compare interactive speed again after disabling logging.

The headless GUI profiling test verifies reporting, sampling, toggle lifecycle,
append behavior and preservation of an active session on log-open failure. It
uses only a scratch directory and no firmware or Yamaha libraries.

## 2026-10-14 slowdown capture

The first real capture contained 96 approximately one-second intervals. Median
emulated speed was 95.6%, with a 70.1% minimum. The interpreter thread used a
median 937.5 ms CPU time per interval. Packet queue lock counts and wait time
were zero throughout; GUI command lock waits never exceeded 2.098 ms. This
points to CPU-thread throughput, not packet-thread contention, in this capture.
Audio waits overlap slice time and are not additional time to sum with it.

Release now enables MSVC `/GL` and linker `/LTCG` in addition to `/O2 /MT`.
A deterministic, silent XEDK1 probe warmed up for 250 million machine steps,
then measured 50 million steps from the same read-only NVM input:

| Build | Seconds | Million steps/sec |
|---|---:|---:|
| `/O2 /MT` | 3.865 | 12.936 |
| `/O2 /GL /LTCG /MT` | 3.587 | 13.939 |

This single-run comparison measured approximately 7.8% higher throughput. Both
produced CPU/LCD checksum `a2f5c9a5`, CPSR `60000033`, 299994267 instructions,
300000000 scheduler cycles and 28273106195 emulated nanoseconds. This is not an
audio-inclusive benchmark or a promise of identical gain on every host/workload.

`build/perfbench.exe MODEL.cfg READ_ONLY.snv` repeats the benchmark without
writing input storage. It requires local ROMs and an existing matching snapshot.
The earlier 256-entry PC table dropped many samples during busy sections; the
larger, bounded-probe sampler improves coverage without altering emulated time.

The final full Release build recheck measured 3.330 seconds / 15.014 million
steps per second with exactly the same checksum and counters. Timing varies
with host load; these silent-probe results do not measure interactive audio
performance. The full Release regression suite also passed.
