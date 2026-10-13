# MA-5 native playback and ABI results

## Accepted pair

The initial brief named 2.0.1.4, but the supplied files are newer/different
versions. The user explicitly accepted the supplied updated pair on
2026-10-09. Both **hash and exact four-part file version** are checked before
`LoadLibraryExA`; no automatic hash learning or bypass switch is provided.

| DLL | File version | SHA-256 |
|---|---|---|
| `M5_EmuSmw5.dll` | `2.2.0.0` | `9bd7a8e24f9fb6430b66e3a11d9f55b98549caf0ffeb42a005da851b316eac7b` |
| `M5_EmuHw.dll` | `1.6.0.0` | `f2702720d01730b3555d042cd22edd5ff58066b59da8f11241236170f26c4732` |

The loader uses names, not ordinals. The hardware DLL is hash-verified and
preloaded by absolute path; the software DLL is loaded from the same supplied
directory. Compiler architecture is x86. API argument counts and lifecycle
come from the supplied [native-file-path notes](../MA5libs/doc/README.md),
then were exercised against this pair rather than assuming compatibility.

## Calling convention: __cdecl

[Ghidra ABI evidence](ma5-abi-ghidra.txt) records the actual decoded return
instructions, including targets of the Initialize/EmuTerminate tail jumps.
Examples at the DLL's preferred image base:

| Export | Wrapper / body | Return |
|---|---|---|
| `MaSound_Create` | `0x100A71B0` | `0x100A71E6 RET` |
| `MaSound_Load` | `0x100A71F0` | `0x100A7218 RET` |
| `MaSound_Control` | `0x100A7270` | `0x100A72BA RET` |
| `MaSound_Start` | `0x100A7360` | `0x100A73A5 RET` |
| `MaSound_Initialize` | tail jump to `0x100A6910` | `0x100A6992 RET` |
| `MaSound_EmuInitialize` | `0x100A8C10` | `0x100A8DB7 RET` |
| `MaSound_EmuTerminate` | tail jump to `0x100A8780` | `0x100A8855/885B RET` |

No immediate stack-pop operand appears in those returns. Runtime confirmation:
MSVC x86 `/Od /RTC1` plus inline-assembly ESP snapshots around each setup/
control/start call; **1,000,000 consecutive volume-control calls while playing,
no ESP drift or runtime stack failure**. Stop/Close/Unload/Delete/Terminate
each returned zero in the completed tests. These results apply to the pinned
pair; they do not certify every historical Yamaha DLL version.

## Embedded voice and host audio test

Test input: 15,410-byte complete MMMD file extracted from first-image offset
`0x418CCE` using `fwinspect --extract-mmf`. It contains `MTR` and a 66-byte
`Mtsu` setup chunk with Yamaha SysEx data, including an embedded voice payload.
No separate bank file is opened or supplied by SGHemu's probe.

The probe uses exactly the supplied README's native path:

1. Allocate a persistent zeroed 1024-byte work block; find a pointer in that
   block whose low address byte is `0x81`.
2. `EmuInitialize(48000, 2, magic)` and `Initialize(0, work, 0)`.
3. `DeviceControl(0x0D,0,0,0)` and controls `0x05/06/08/09`.
4. `Create(1)`, `Load(1, whole_file, length, 1, NULL, 0)`, `Open`, `Standby`.
5. `Control` for volume 100, key 0, tempo 100; `Seek`; `Start(1,1,1,0)`.
6. Stack stress; record ten seconds of default-render-device WASAPI loopback.
7. `Stop -> Close -> Unload -> Delete -> Terminate -> EmuTerminate`, then free
   host buffers and DLL handles. On cleanup failure, retain resources until
   process exit rather than free data the library may still use.

Observed capture: 3,836,160 audio bytes, peak `0.636233`, 951,912 nonzero sample
values, saved to ignored `build/ma5-loopback.wav`. Ghidra also identifies
`DirectSoundCreate`, `CreateThread` and multimedia timing imports. Together,
native playback plus the non-silent loopback demonstrates a host-driven
output path for this test: the probe never requests or feeds PCM via
`MaSound_Generate`. The recording contains all desktop audio, so it is not an
isolated per-process measurement, nor an exhaustive proof for every SMAF
voice format. Test other embedded-FM files before claiming broad compatibility.

## Experimental MA-3 hardware integration (2026-10-10)

The emulator now uses **guest byte ports -> MA-3/MA-5 translation ->
M5_EmuHw.dll -> caller-generated PCM -> WinMM WaveOut**. It does not intercept
high-level guest playback calls or hand a complete MMF to the software DLL.
The original file-playback probe above remains a separate diagnostic tool.

### Recovered interface and ABI

The C100 status/select port is `0x00200000`, data is `0x00200004`, byte access
only. XEDK1 wrappers are at `0x01234612/0x01234628` (status) and
`0x0123463E/0x01234654` (data). `YMU_InitializeInterrupt` routes the device to
interrupt-controller bit 23 (`0x00800000`). Bring-up profiles configure those
addresses explicitly; other models need their own mapping.

`MaDevDrv_Fifo` (`0x011C754E`) cycles four 128-byte staging buffers, filled by
`MaSndDrv_UpdateSequence` (`0x01243B58`). Observed packets contain note,
channel, RAM and control commands, **not a whole MMMD file**. The firmware
handles SMAF sequencing/loop policy; the hardware engine consumes timed packets.

The pinned hardware DLL's exported wrappers have plain `RET`, not `RET n`.
Local Ghidra analysis (`tools/ghidra/Ma5Hardware.java`) and an x86 runtime test
with 1,000,000 status reads without ESP drift confirm `__cdecl` for this binary.
Recovered signatures, exercised by `ma5hwprobe`:

```c
int __cdecl Hw_Initialize(unsigned rate, unsigned char mode,
                          void (__cdecl *irq)(void));
void __cdecl Hw_Terminate(void);
void __cdecl Hw_Generate(int16_t *left, int16_t *right, unsigned frames);
void __cdecl Hw_WriteStatusFlagReg(unsigned char value);
void __cdecl Hw_WriteDataReg(unsigned char value);
unsigned char __cdecl Hw_ReadStatusFlagReg(void);
unsigned char __cdecl Hw_ReadDataReg(void);
```

Initialization uses 48000 Hz, mode **0**, a no-argument IRQ callback, and requires
return **0x46**. `Generate` returns planar signed 16-bit stereo and advances the
chip sequencer. Buffer boundary sentinels are checked on every call. Hardware
mode needs only `M5_EmuHw.dll`; software/native-file mode still needs the pair.
Loading, hash/version verification, every DLL call, PCM output and teardown run
on a single dedicated audio thread. No proprietary binaries are bundled.

### Translation, not MA-3 binary compatibility

MA-5 is **not** a drop-in MA-3 register/RAM implementation. MA-3 parameter reads
use RBO1/2/3; native MA-5 only implements the required readback through RBO1 and
has different reset defaults. The adapter maintains actual shadow parameter
state, with the MA-3 defaults checked by the guest driver (channel volume 0x60,
pan 0x3C, global defaults 0x40). Writes and reset change that state; no guest
verification routine is patched or given a forced return value.

Observed translations:

| MA-3 | MA-5 | Status |
|---|---|---|
| Parameters 0x000..0x08F | Same address | First FM/PCM voice blocks |
| Parameters 0x090..0x13F | Address + 0x30 | Channel controls and second FM block |
| Voice pitch high bytes | Swap bits 0..2 with 3..5 | FM/PCM octave and upper FNUM fields differ; preserve bits 6/7 |
| NOP 0x148 / 0x166 | 0x1DF | Explicit guest NOPs |
| Divider 0x15E/15F | Bank-0 registers 0x23/24 | Repack 14-bit count and convert 1157 ns/count to native 1085 ns/count |
| FIFO threshold 0x160 | Register 0x25 | Native value selects `(value & 7) * 64` bytes |
| Sequence start/stop 0x161 | Register 0x26 | Starts/stops native sequencer |
| Interrupt parameter 0x162 | Registers 0x11/12 | Translate MA-3 source numbering; FIFO bit 7 enabled |
| Secondary timer 0x152/154/155 | Registers 0x2E/2F/30 | Independent timer: prescaler, reload, run |
| Soft IRQ payloads 0x15A..15D | Parameters 0x1D6..1D9 | ISR low/high source bits translated |
| Bank-0 volume register 7 | Output 0x27/28 and synth 0x29/2A gains | Provisional shared curve; separate gain controls unresolved |

The delayed FIFO preserves delta-time bytes and translated note payloads,
including the note gate. Three-byte addressed full-RAM packets are forwarded
with their lengths and literal bytes. An unsupported delayed control becomes a
NOP **with its original delay preserved**. Unsupported immediate parameters are
counted and retained in shadow state, not assumed implemented. Bounds/malformed
packet or backend-output errors stop execution with diagnostics.

**FM bank 2 requires the 32-voice select.** The pinned DLL constructs its FM
engine at sixteen voices (`FUN_10015670` gets param 1 -> `this+0x1804 = 0x10`)
and `FUN_10016094` silently drops every key-edge for `voice >= 0x1804`. The
MA-3's second FM bank sits on native voices 16..31 (0x110..0x16F), so without
arming register 0x38 bit0 = 1 those notes vanished entirely ("voice cap"
symptom). `ma3_enable` writes `0x38 = 1` (case 0x38 -> `FUN_10016198(engine,
~v&1)`: 0 -> 32 voices). Verified by reading the engine counter before/after:
16 -> 32. The `HW reg 38` diagnostic stays 00 — that mirror is never stored by
case 0x38 and does not reflect the real engine count.

Native dispatcher `FUN_10004834` routes 0x00..0x5F and 0x110..0x16F to
`FUN_100036b8` (FM voices 0..15 / 16..31), 0x60..0xBF to `FUN_10003804` (PCM
voices 0..15), 0xC0..0x10F to the channel groups, 0x170..0x1CF to
`FUN_10003804` (PCM second block, 0x1A6..0x1B1 are the two stream voices), and
0x1D0+ to soft/auxiliary controls. The MA-3 WT map (0x060..0x08F identity) is
therefore correct.

MA-5 reset leaves synth and output gains at zero; translating only the DAC gain
therefore produced entirely silent PCM. Translating the provisional shared gain
unblocked real guest-generated startup notes. This is not yet a calibrated
volume curve or a claim that every MA-3 voice/configuration format works. The
startup still encounters 16 unsupported immediate parameters. PCM sample modes,
extra synth controls, all melodies, game sound and loop behavior need more tests.

### Recovered clock and pitch corrections

The first integration incorrectly copied the divider bytes into MA-5 registers.
XEDK1 `MaSndDrv_SetSpeed` (`0x01247050`) packs a count as two **seven-bit**
fields. Its constants at `0x01247290/94` are 100000000 and 1157. The pinned
hardware DLL instead decodes register 0x23/24 as `(high << 6) | (low >> 2)`;
its period calculation at `0x10005DC0` uses **1085 ns/count**. The adapter
now converts units, rounds to the nearest count, and rejects overflow. The
firmware's `1B,00` becomes native `39,94`: approximately 4 ms instead of the
previous 1.875 ms. The model config supplies the 1157 ns quantum explicitly.
This corrects sequencer translation; it does not establish CPU clock speed.
Separate [PLL analysis](c100-clock.md) now establishes firmware-reported mode-2
rates of 23.4 MHz for ARM and 46.8 MHz for DSP.

Pitch was independently wrong. The C100 `GetFmBlockFnum` (`0x012432EA`) table
and `GetWtBlockFnum` (`0x01243458`) pack a pitch word as
`(FNUM & 0x380) << 3 | BLOCK << 7 | (FNUM & 0x7F)`.
MA-5's FM/PCM decoders (`0x100036B8/0x10003804`) expect the upper byte's
octave and upper FNUM fields in the opposite order. Only voice-parameter
pitch high bytes are transformed; literal voice/sample RAM remains untouched.
Both immediate writes and delayed bursts use this translation, including
fragmented packets and pitch-only updates. Guest readback retains MA-3 values.

For example, the startup's MA-3 `2C,4A` means BLOCK 4, FNUM 714 (C5), so the
native bytes are `25,4A`. Previously MA-5 interpreted BLOCK 5, FNUM 586.
The corrected capture has a spectral component around 523 Hz instead of
around 859 Hz. This is a bit-format correction, not resampling or a guessed
semitone offset. Startup activity remains 3.818979 seconds after this change.

The software DLL was also inspected with `tools/ghidra/Ma5Software.java`.
Its loader resolves the `Hw_*` exports dynamically, and its write wrappers
maintain a hardware-protocol shadow. These dispatch/caller traces complement
the hardware decoder analysis; the emulator does not call software-DLL internals. The WT cursor bridge described
below now uses a read-only layout hook in the exact hash-pinned hardware DLL.

### Usage and timing experiments

In the GUI select **Emulation > Import Yamaha hardware DLL...**, choose the
pinned `M5_EmuHw.dll`, then press F5 after the reset. The experimental-audio menu
item toggles audio with a reset. Silent mode still works without the DLL. Path
and enable setting are saved locally in ignored `state/audio.ini`.

```ini
[audio]
hardware_dll=E:\your-path\M5_EmuHw.dll
enabled=1
ticks_hz=2166667
```

CLI examples (existing NVM auto-loads by default):

```powershell
.\build\ma5hwprobe.exe MA5libs/M5_EmuHw.dll
.\build\sghemu.exe models/c100/c100xedk1-bringup.cfg 30000000 --ma5-hw MA5libs/M5_EmuHw.dll --audio-wav build/startup.wav
.\build\sghemu.exe models/c100/c100xedk1-bringup.cfg 30000000 --ma5-hw MA5libs/M5_EmuHw.dll --audio-ticks-hz 4333334
```

`--audio-wav` selects deterministic offline capture instead of speakers.
Without it, WaveOut supplies host pacing/backpressure. Ten-millisecond blocks
and eight output slots cover a normal 16 ms GUI tick. Pausing stops emulated chip
time and pauses WaveOut; reset tears down and recreates the backend. Traces:
`build/ma3-traffic.log` (CLI), `build/gui-ma3-traffic.log` (GUI). First-boot
provisioning resets recreate the trace/WAV, so a WAV records the last boot,
not all provisioning attempts. Use an initialized save for comparisons.

The default 2,166,667 **interpreter ticks/sec** is a diagnostic starting point:
10,000 frame-IRQ ticks divided by approximately 4.615 ms. It is **not CPU MHz**
or measured ARM cycle timing. Changing it changes the relationship between
firmware execution, frame interrupts and generated samples. MA-5 also has its
own sample-driven sequencer clock programmed by firmware; stutter can instead
come from FIFO starvation, host output scheduling or insufficient CPU throughput.
Use the no-input startup tune plus a real-phone reference to compare tempo,
pitch, duration and refill gaps. Audio alone cannot determine the physical CPU
clock until timer/clock configuration and interpreter cycle accounting agree.
C100 PLL profiles now use estimated core cycles and emulated nanoseconds for
audio and the diagnostic GSM-frame IRQ; the INI/CLI instruction-tick override
is used only by legacy profiles without a clock model. External bus waits,
SIM/DMA calibration and complete DSP timer modeling remain outstanding. See
[c100-clock.md](c100-clock.md).

### Recorded checks

- Synthetic protocol suite: 267 checks, no DLL/firmware needed; total build suite
  now 1,193 checks. Covers readback/reset, divider and pitch translation, split FIFO packets,
  preservation of unsupported-command delays, emulated-time PCM and IRQ routing,
  wrong-width accesses and bounded packets.
- Live RBO1 reads now query native parameter/sample RAM without advancing PCM time.
  Voice pitch high bytes are converted back to the MA-3 layout. Read-buffer selectors
  mask off acknowledgement bits: the guest uses `0x11`, `0x22`, and `0x43`.
  RBO3 remains the MA-3 parameter shadow needed for reset verification.
- The standalone hardware probe passes generated ADPCM (format 0), 16-bit PCM
  (format 1), and 8-bit PCM (format 2) sample tests with nonzero output and intact
  buffer guards. This confirms native decoding, not complete guest streaming support;
  MA-3 stream commands, positions and completion IRQ translation remain unfinished.
- The user confirmed MA playback no longer stutters after increasing GUI refill
  work and adding scoped multimedia timer resolution. Ringtone-change acceleration
  and PCM/ADPCM playback through the phone remain under investigation.
- Corrected XEDK1 / XEDA2 / XEDD1, 35 million instructions, completed NVM,
  offline: each produced 775,200 stereo frames, **366,428 nonzero sample
  values**, peak 11,528, and 645 IRQs. Startup activity spans 3.818979 seconds;
  the longest interior all-zero gap is two samples (42 microseconds), not an
  audible dropout. No backend failure or partial FIFO packet remained.
- Before the corrections, GUI import with XEDA2 and the user's existing NVM reached standby with
  1,435,680 PCM frames, 177,986 nonzero values, peak 10,579 and no audio error.
  F6 paused execution; F5 resumed with FIFO traffic continuing. The NVM differs
  from the fixed offline fixture, so these counts are not a golden comparison.
- Speaker underruns are counted separately from guest/chip behavior and shown
  in CLI/GUI diagnostics. A corrected XEDK1 35-million-instruction speaker run
  produced the same 775,200 frames / 366,428 nonzero values / 11,528 peak with
  **zero host underruns** and no backend failure. Zero offline underruns alone
  is not a speaker validation;
  matching a real phone's complete sound/timbre remains unverified.

WAVs, raw decompilation and firmware-specific traces remain ignored local
artifacts. No standalone duration parser is needed for this hardware route:
the DLL's chip sequencer and IRQ callback are driven by generated sample time,
not the software/native-file API's unrecoverable end-of-song behavior.

## Distribution and acquisition

DLLs, MMF/voice files, firmware and captured WAVs are excluded by `.gitignore`.
No separate redistribution license was supplied for the proprietary libraries;
SGHemu does not redistribute them. Import manually or configure an existing path.
No download was requested or performed. Any future downloader must obtain explicit
consent, extract only the requested DLLs, verify pinned hashes and fall back to
manual import on failure.


### Sample and stream investigation

`ma3pcmprobe` exercises the actual MA-3 ports through the pinned hardware DLL,
without ROMs or copyrighted sample assets. Literal full-RAM descriptor/data
writes produce nonzero output for ADPCM4, PCM16 and PCM8. This verifies basic
codec transport, **not** complete firmware stream playback.

C100 `MaSndDrv_StreamOn` (`0x01245A44`) queues stream-start payloads at MA-3
`0x15A/15B`. These now translate to native `0x1D6/1D7`, preserving FIFO delta
and payload. Native `0x10004418` stores the payload and raises ISR bits 2/3;
guest live readback uses the corresponding mapped native parameter. MA-3 stream
IRQ bits are **0/1**, not native bits 2/3: the adapter translates status, enables
and acknowledgements. Ghidra `MaDevDrv_SoftInt0/1` (`0x011C72BA/DC`) confirms
the callback routing. The real DLL probe observes payloads 43/44 and guest ISR
81/82 at 12 ms for a 10-tick event. Timer A native bit 4 maps to MA-3 bit 5.

The secondary timer now runs independently of sequence playback. C100 writes
prescaler 27 and reload 20; the native timer formula is 37,109 ns * prescale *
reload (20.038860 ms). The real-DLL probe observes MA-3 bit 5 at 21 ms, verifies
acknowledgement and stops the timer. Firmware `MaDevDrv_Timer0` (`0x011C7342`)
calls the stream refill handlers and secondary-sequence update.

Live position at MA-3 `0x14A..151` now reads the native decoder cursor instead of
reset shadows. No public `Hw_*` export exposes WT position. A worker-only,
read-only hook uses the pinned DLL's singleton RVA `0x101780`, PCM pointer
field `+0x124`, 32 channel records of stride `0x80CC`, format `+0x8029` and Q16
sample cursor `+0x808C`. Ghidra `0x10001AD0`, `0x1001C4E8`, `0x1001C13C` and
`0x1001B324/5CC/820` establish this layout. Address ranges/protection are checked
with VirtualQuery, invalid voices/layout return failure, and no private function
is called. The stream-cache correction below adds one narrowly scoped native
state write. These hooks are **not portable across DLL
builds**; the existing exact hash/version refusal is essential. Tests verify
format/cursor values for all three generated codecs and reject voice 32.

The cursor is converted to byte position, includes the MA-3 ring's 0x20-byte
sample offset, wraps at 0x400 and is quantized as expected by StreamUpdate.
An isolated Arabesque run (Menu -> 3 -> Ring Tone) observes 121 live cursor
reads and 129 sample/voice RAM writes, including repeated 64/96-byte ring refills
and a 992-byte initial load. The firmware reaches its stream handler without
patching firmware or changing the user's NVM. This establishes guest refill
activity; audible correctness, stream-control 0x159 gating, timer-clock precision
and the reported fast initial/game playback still require verification.

`ma5seqprobe` tests a 100-tick event across native FIFO clears and restarts:
102 ms initially, 101 ms after FIFO clear/start, and 102 ms with counter reset.
This empty-FIFO experiment does not cover an interrupted tune: the native clock
stops advancing while the FIFO is empty. Plain stop/start must preserve timing.

### Sequencer reset translation (post-v0.2.0 test build)

Ghidra confirms `MaDevDrv_ClearFifo` (`0x011C77BA`) pulses port 4 with `0x28`.
`MaDevDrv_Fifo` (`0x011C754E`) recovers an underrun with stop, clear `0x20`,
refill, pulse `0x08`, then start. Native MA-5 bit 3 resets only the parser;
it leaves the accumulated Q16 tick count and cumulative packet deadline intact.
Native `0x26` bit 1 resets both (`0x10005E08` and its register dispatcher).

An interrupted-sequence probe queues events at 100 and 1,100 ticks, advances
500 ms, stops/clears, then queues a new 100-tick event. Pass-through reset plays
that event after **1 ms** instead of 100 ms; native counter reset gives **102 ms**.
The adapter now translates the rising MA-3 `0x08` pulse into a native counter
reset, retaining the guest run bit. It does not reset on ordinary pause/resume,
change sample rate/pitch, or change the independent stream-refill timer.
`ma3pcmprobe` verifies the guest-facing recovery sequence produces its 100-tick
payload after 102 ms. Real ringtone/game tempo still needs playback verification;
this correction is not included in the published v0.2.0 snapshot.

The isolated firmware-driven Arabesque regression completes without CPU/audio
errors. Its opening 992-byte sample load now happens on the first sequence
start (instruction tick 267,513,535 rather than 323,072,364 in the pre-fix run),
followed by 98 live position reads and repeated sample refills. This demonstrates
that the opening is no longer skipped; it is not a physical-handset tempo test.

```powershell
.\build\ma3pcmprobe.exe MA5libs/M5_EmuHw.dll
.\build\ma5seqprobe.exe MA5libs/M5_EmuHw.dll
```

CPU/DSP navigation tones are a separate path: `kid_keybeep` (`0x011443FC`)
calls `audioStartTone` (`0x011D3DBA`) for ordinary tones; `TGSetToneD1`
(`0x012AF30C`) programs tone state. This path is now rendered separately
from Yamaha using the model-configured DSP tone table; see
[CPU tones and service LED](c100-phone-output.md). It is not replaced with
host keypress beeps.

### ADPCM stream-cache correction (v0.2.2 experimental build)

The user confirms improved playback speed, but reports repeating stream openings.
Ghidra `0x1001BCE4` confirms a decoded ADPCM sample cache: the decoder only reads
compressed RAM for indices at/above its next-decoded index, otherwise returning
cached PCM. Loop wrap does not rewind that index. RAM refills therefore cannot
replace already cached audio, despite observed guest refill activity.

Completed immediate RAM packets now notify the audio worker of their full range.
Writes overlapping a live ADPCM voice's already decoded input mark that cache
dirty. Generation splits at the voice's loop boundary, then resets only its
next-decoded index (channel `+0x14`) to zero. The predictor, step size, envelope,
sample cursor and key-on state are preserved. Normal unmodified sample loops and
PCM8/PCM16 are untouched. This is a **write hook into the exact hash-pinned DLL**,
not a portable exported API. Readability/writability are checked before access;
an unavailable layout fails audio generation instead of guessing.

The generated-ADPCM probe modifies compressed RAM during playback and verifies
exactly one cache refresh; pitch, stream IRQ, refill timer and sequence-reset
probes still pass. Delayed FIFO packets are sent in one synchronous worker command
instead of a thread handoff per byte, retaining packet order and buffer lifetime.
Three-byte addressed delayed (FIFO) RAM packets now mark the written window stale
too, mirroring the immediate path: the length field is consumed before the send,
so the saved `delayed_total` drives `ma5_hw_ram_changed` (immediate and delayed
paths now share refresh semantics; the firmware refills use the immediate path).
This reduces scheduling overhead; eliminating host underruns and completing
instrument/control compatibility are not claimed.

The isolated firmware-driven Arabesque run completes without CPU/audio errors
and exercises eight ADPCM cache refreshes. The opening sample is still loaded
on the first start, and the guest continues its ring-buffer refills. The user
has not yet confirmed this build's audible stream/instrument behavior.

Post-v0.2.2 click correction: the native ADPCM interpolator blends the final
sample against cached loop-start PCM before wrapping. For a dirty/refilled cache
that lookahead belongs to the previous lap. Generation now splits before this
last sample and holds the decoded tail for its fractional lookahead; after wrap,
the existing cache refresh decodes new input with the same predictor/step state.
No global smoothing, pitch change or voice restart is applied. Audible results
still need confirmation; host underrun clicks are a separate remaining issue.

### Silent first ringtone pass: fixed (2026-10-13)

A firmware-driven reproduction boots XEDK1 from a read-only NVM snapshot,
presses Right then Up four times, verifies the LCD shows Peer Gynt, and captures
65 seconds of playback. Before the fix the first playback second contained zero
nonzero PCM samples and output remained silent until about 34 seconds after boot.
After the fix the same first playback second contains 95,982 nonzero stereo
samples (out of 96,000); the first pass now produces audio.

The cause is startup FIFO scheduling, not the MMF or missing FM voices.
`MaSndDrv_SeekControl` feeds initial channel setup, starts the MA-3 sequencer,
calls `Ma3_WaitDelayedFifoEmpty`, stops it, and clears the FIFO. The wait polls
only sixteen times, completing before our next 96-frame PCM step. Previously
the native DLL had not processed the setup at all when the guest cleared it.
Channel gains consequently remained at zero until loop two supplied them again.

Starting the sequencer now calls `ma5_hw_pump`: `Hw_Generate(NULL,NULL,0)` on
the dedicated DLL worker executes already-due commands without emitting PCM or
advancing sample time. The native implementation explicitly supports zero-frame
calls (`FUN_10001ad0`). No firmware patch, forced FIFO status, guessed control
register mapping, or extra audio-time advance is involved.

The real-DLL `ma5seqprobe` asserts that a queued zero-delay volume write is
applied and the FIFO is empty with zero PCM frames generated. It also pumps a
100-tick delayed event and verifies it still fires at 101-102 ms, not early.
The firmware reproduction is `tools/ringtone_boot_probe.c`; captured evidence
is local/ignored under `build/peer-baseline-original.*`, `build/peer-pump-run.txt`,
and `build/peer-boot.wav`. It never saves or modifies the user's NVM snapshot.

The older streamed-ring investigation below concerns additional historical
captures; its open hypotheses are not evidence against this measured startup fix.

### Streamed-ring stalls (2026-10-13 investigation)

The remaining `Peer Gynt.mmf` defect is a repeatable "plays a few seconds, then
several seconds of silence, then resumes" pattern. `tools\ma3replay.c` at the
documented 2,166,667 ticks/sec produces these output runs (48 kHz frames):
silent 0-69,408; audio 69,408-292,032; silent 292,032-326,208; audio
326,208-452,352; silent 452,352-728,736; audio 728,736-833,760; silent
833,760-853,056; audio to the end.

Established from the replay trace, `MA5HW_STATE` dumps and Ghidra:

* **The silence is a frozen native engine, not a deadlock.** During the first
  silence the per-voice decoder phase `ph` is *byte-identical* at GEN
  288000/300000/312000/324000 while `FIFO fill=0`, and playback resumes on the
  dump where the guest refills (`fill` 0 -> 128 at GEN 324000). The engine only
  runs while the host feeds the delayed FIFO, so an empty ring is heard as
  silence.
* **The exact refill algorithm** (`MaDevDrv_StreamUpdate`, `0x011C6A06`,
  decompile line 210207):
  1. `uVar6 = MaDevDrv_ReceiveData(0x151-ch,0)` -- the live PCM-cursor read
     (status-3 packet, address bytes `50|51 82`, buffer select `11`).
  2. Formats below 2 convert the answer to a byte position as
     `(uVar6 & 0x3E) << 4`; ADPCM uses `(uVar6 & 0x1F) << 5`. The first form is
     a **32-byte-granular** offset spanning `0..0x3E0`.
  3. If it equals the previously read cursor (`+0x242`) nothing is sent.
  4. Otherwise the target is `uVar6 == 0 ? 0x3C0 : (uVar6 - 0x20)`; the guest
     fills the 0x3E0 ring at `(7-ch)*0x400 + DAT_011C6C80` from last position
     `+0x23E` up to that target, wrapping, via `MaDevDrv_SendDirectRamData` plus
     `MaDevDrv_SendDirectRamVal(pos,count,0x80)`.
  5. State 2 -> 3 when the remaining byte count `+0x250` reaches zero, recording
     the cursor in `+0x264`.
  6. State 3 -> 0 when the current cursor is **cyclically between** `+0x260`
     (`0x3E0` or `0x3E0 % initial_length`) and `+0x264`, that is one of
     `(u2 < c < u3)`, `(c < u3 < u2)`, `(u3 < u2 < c)` on `ushort` values. On
     that edge the channel's run bit is cleared in `+0x24`, a `55 82 80`
     (0x155 = 0x80) packet is sent once every channel has stopped, the position
     register `(7-ch)*6 + 0x65` is read back and sent, and, when the global
     `+0x27 == 1`, `MaSound_ReceiveMessage(2,0,0x80)` is called.
* Our synthesized answer (`src\ma3.c` cursor branch) is
  `0x40 | ((byte + 0x20 & 0x3FF) >> 4 & 0x3E)` for formats 0/1, so the guest
  reconstructs `((byte + 0x20) & 0x3FF & ~0x1F) - 0x20`: a 32-byte ladder whose
  phase against `+0x260`/`+0x264` depends entirely on the `+0x20` bias and the
  `0x400` (not `0x3E0`) mask. Because the wrap test uses *strict* comparisons on
  that ladder, a wrong bias/unit makes a segment end early or never. This is the
  prime remaining suspect, and the reason the readback is now instrumented
  (`MA3_CURSOR_TRACE`).
* `tools\cursor_ladder.py` decodes all 53 logged readbacks into the ladder the
  guest actually sees. During the frozen opening the answer is constant
  (`raw=0`, `uVar6=0x20`), so `uVar6 == +0x242` every time and the driver
  correctly sends nothing. From ts 156839410 the ladder advances in ~0x180-0x1C0
  steps and wraps repeatedly (observed set `0x20..0x3E0`), so the refill does
  work once the engine runs. Two **6.09 s** read gaps (before ts 156839410 and
  before ts 174948805) bracket the long silences -- but in both cases the audio
  marker resumes ~0.6 s *before* the cursor reads do, so the streamed-ring refill
  is not what restarts playback; the delayed-FIFO sequence feed is.
* `tools\cursor_timeline2.py` shows the cursor reads stop for 5.02 s / 5.05 s /
  3.34 s / 2.90 s / ~0.99 s windows that bracket every silent run, i.e. the
  driver leaves the streaming state (or stops being called) for exactly the
  silence.
* **The native "IRQ" is a per-chunk work nudge, not a timer.** The callback the
  DLL invokes is `FUN_100054E4`, called from inside `Hw_Generate` once per
  48-frame chunk. It sets flag bit 7 in the DLL word at `+0xC8` when the
  immediate queue count is at/behind the delayed-ring count and then calls the
  host callback if `+0xCC != 0 && (flags & +0xCA) != 0` (decompile
  `M5_EmuHw.c` 2814-2835). Our wrapper collapses this to one bit,
  `Ma5Hw.irq`, and `ma3_advance` forwards it to the guest INTC without
  differentiating the source; `FUN_10002608` (the guest status read) does not
  expose the `+0xC8` flag word. `MA5HW_STATE` now prints
  `CALLBACKS total=... reads=... hits=... flag=...`; the whole run produced
  16,096 DLL callbacks but only 99 sampled IRQs because in the offline replay
  `ma3_advance` runs per *trace line*, collapsing each burst. In the GUI
  `ma3_advance` runs once per instruction (`machine.c:377`), so the guest would
  actually see this callback on the order of once per millisecond of emulated
  time -- about 20x the documented ~20 ms Timer A -- with no way to tell which
  source fired. Timer A programming does reach the chip (0x152 -> native 0x2E,
  0x154 -> native 0x2F), so a mis-modelled interrupt cadence/source is a strong
  second suspect.

* **The guest itself mutes Timer A for the whole of every silence.** The replay
  writes 0x162 (the IRQ enable) exactly ten times, alternating between two
  values, and the two values straddle every silence and every resumption:

  | tick | value | guest bit 5 (Timer A) | following AUDIO marker |
  |---|---|---|---|
  | 143714282 | 0x9F | off | (stream idle before the first note) |
  | 144584278 | 0xFF | on  | pcm 144630494 |
  | 154594889 | 0x9F | off | silent 154608263 |
  | 156071394 | 0xFF | on  | pcm 156169484 |
  | 161830584 | 0x9F | off | silent 161844926 |
  | 162703832 | 0xFF | on  | pcm 162746- |
  | 172772547 | 0x9F | off | (pre-roll) |
  | 174239235 | 0xFF | on  | pcm 174336484 |
  | 179046292 | 0x9F | off | silent 179068396 |
  | 179913237 | 0xFF | on  | pcm 179958396 |

  `native_irq_low(0x9F) = 0x8C` and `native_irq_low(0xFF) = 0xBC`; the single
  differing native bit is 4, i.e. the documented "Timer A native bit 4 <->
  MA-3 bit 5". So each silence begins when the firmware *clears* its own Timer A
  enable and ends when it sets it again -- the missing audio is a deliberate
  guest stop, not the chip starving the host. `AUDIO silent` lands 13-24 ms
  after the mask-off write and `AUDIO pcm` 58-100 ms after the mask-on write.
  The `| 128` we OR into native register 0x11 is therefore a no-op (the guest
  always sets bit 7 anyway) and is *not* the cause.
* **The stop/start call sites are pinned down, which is the real payload here.**
  `MaDevDrv_StopSequencer(0,*)` is `ControlInterrupt(1,0x60)` -> mask 0x9F and
  `MaDevDrv_StartSequencer(0,*)` is `ControlInterrupt(0,0x7F)` -> mask 0xFF, so
  every silence/resumption pair *is* a Stop/StartSequencer pair. Their callers:
  - `MaSndDrv_ControlSequencer` (0x0124690C): `case 0` -> `StopSequencer(param_1,1)`
    and `case 1` -> `MaDevDrv_Fifo(*DAT_01246ea0)` then `StartSequencer(param_1,1)`
    (lines 320421-320489). `case 4` (reset) leaves `*DAT_01246ea0 = 2`, which is
    what makes `case 1` take the `Fifo` detour at all.
  - `MaDevDrv_Fifo(param_1)` (0x011C754E) has **two independent stop paths**:
    1. line 210662, unconditional: `if (*(char*)(*+0x28) == 2) { *(+0x28) = 0;
       StopSequencer(0,1); ... }`. Invoked when the player arrives with nothing to
       push -- `+0x28` is stepped 1 -> 2 at lines 210719-210722 precisely when
       `local_18 == 0 && local_1c == 0 && iVar5 == 0 && *(+0x28) == 1`. This is the
       end-of-buffered-data stop.
    2. line 210679, guarded: `if ((*(char*)(*+0x23) != 0) &&
       Ma3_CheckDelayedFifoEmpty()) { StopSequencer(0,0); *(+0x22) = 1;
       pulse 0x20; }`, followed at lines 210710-210718 by the `+0x22`-triggered
       `pulse 0x08` and `StartSequencer(0,0)`.
    Path 2 is the classic anti-underrun guard ("data arrived but the chip already
    drained -- stop, flush, restart"). Path 1 is the one that fires in the stalls
    and it does **not** consult the FIFO at all. That also closes out the old
    `MA3_MASK_FIFO_EMPTY` negative result: forcing status bit 1 clear only
    disables path 2, so the trace length and every AUDIO marker stayed identical.
  `MaDevDrv_Fifo`'s loop also calls `MaSndDrv_UpdateSequence(0, buf, 0x80)`
  (line 210697), the producer that actually fills each packet; its return value is
  what decides `iVar5` and therefore whether the player counts as "out of data".
  The next thing to instrument is `MaSndDrv_UpdateSequence`, not the FIFO.
* **Inside the silence the guest scans the parameter bank slowly.** Decoding the
  status-3 address bytes in the 0x9F..0xFF window gives 65 reads: field 5 of
  every 6-register voice block (`5 11 17 ... 125`, all `%6 == 5`), then field 3
  of the first three (`3 9 15`), then addresses 16-47 -- 32 consecutive
  registers, one per voice. Only ~46 reads/s, i.e. ~21 ms apart, so this is a
  paced poll, not a spin. The old suspect "the guest feeds almost nothing"
  is this loop: the firmware parks, waits, and then restarts the tone.

* **NEGATIVE RESULT: the MA-3 IRQ model is faithful; do not change it.** Checked
  end to end against the DLL:
  - `Hw_Generate` ticks all three chip timers once per generated chunk and on
    expiry sets `flags |= 0x10 << timer_index` (`M5_EmuHw.c` 716-721). Timer A
    (index 0) therefore raises native flag **low bit 4**.
  - `Hw_ReadDataReg` -> `FUN_10002738` returns `(flags_word & mask_word)` for the
    selector in `+0xC4`: `+0xC4 == 0` gives the **low** byte, `+0xC4 == 0x10` the
    **high** byte (1225-1257). Our `read_ma3` port-4 path reads exactly those two
    and combines them with `guest_irq`, whose native-bit-4 -> guest-bit-5 term is
    precisely the documented "Timer A native bit 4 <-> MA-3 bit 5".
  - `Hw_WriteDataReg` -> `FUN_10004b04` with `+0xC4 == 0` does
    `flags &= ~param_1` (2485-2503) -- that is the ISR ack, and `+0xC4` is set by
    the port-0 write `Hw_WriteStatusFlagReg(v)` (`+0xC4 = v & 0x3f`,
    `+0xCC = (v & 0x80) != 0`). The firmware's own `MaDevDrv_IntHandler`
    (0x011C6580) is `Ma3_WriteStatusFlagReg(0); uVar1 = Ma3_ReadDataReg();
    Ma3_WriteDataReg(uVar1 & 0x7f);` -- an exact match for that protocol, and its
    dispatch mask `DAT_011c6874 + 0x26` is filled by `MaDevDrv_AddIntFunc`. The
    `MaDevDrv_ControlInterrupt` (0x011C64F0) read-modify-write of 0x162 is what
    produces the alternating 0x9F/0xFF mask values above.
  - `+0xCC` is re-enabled constantly (the firmware emits `W 0 80` before nearly
    every access, from `Ma3_Wait`), so the interrupt is asserted on every chunk
    where `flags & mask != 0`; the DLL's own per-chunk flag is native low bit 7,
    which `guest_irq` maps to guest bit 7. (Bit 7 *is* handled -- see below.)
  - **RESOLVED, definitive (ROM-verified): the guest IRQ bit map is complete and
    our translation is faithful bit for bit.** The eight handler pointers live in
    ROM at `0x011C7D78`; they are Thumb (bit 0 set). `MaDevDrv_Initialize`
    registers them with `MaDevDrv_AddIntFunc(bit, handler)` at
    `C100XEDK1.c:210961-210968`, and the byte image at file offset `0x1C7D78` of
    `models\c100\roms\C100XEDK1\C100XEDK1_1.bin` (ROM base `0x01000000`) reads:

    | guest bit | handler at `0x011C7D78 + bit*4` | payload register |
    |---|---|---|
    | 0 | `MaDevDrv_SoftInt0` (`0x011C72BA`) | 0x15A |
    | 1 | `MaDevDrv_SoftInt1` (`0x011C72DC`) | 0x15B |
    | 2 | `MaDevDrv_SoftInt2` (`0x011C72FE`) | 0x15C |
    | 3 | `MaDevDrv_SoftInt3` (`0x011C7320`) | 0x15D |
    | 4 | **`dummy_IntFunc` (`0x011C60B8`)** | none |
    | 5 | `MaDevDrv_Timer0` (`0x011C7342`), Timer A | - |
    | 6 | `MaDevDrv_Timer1` (`0x011C7388`), Timer B | - |
    | 7 | `MaDevDrv_Fifo` (`0x011C754E`), FIFO service | - |

    The DLL side matches exactly. `FUN_10004418` (`M5_EmuHw.c` 2163-2179) raises
    native **low** bits 2/3 for payloads 0x1D6/0x1D7 and native **high** bits
    2..5 for 0x1D8..0x1DB; `guest_irq` maps low 2/3 -> guest 0/1 and high 2/3 ->
    guest 2/3, so payloads 0x1D6..0x1D9 land on guest bits 0..3 = SoftInt0..3.
    Four for four. `Hw_Generate` raises native low bit 4 for timer 0 and low bit
    5 for timer 1, mapped to guest 5/6. The DLL's per-chunk nudge is native low
    bit 7 -> guest bit 7 -> `MaDevDrv_Fifo`. Nothing is left over.
  - **CLOSED (negative result): guest bit 4 is a dummy and must NOT be driven.**
    Bit 4's handler is `dummy_IntFunc` (`C100XEDK1.c:209626`), a two-byte
    `return;` stub. The firmware leaves bit 4 set in its enable mask (`+0x26`;
    both 0x9F and 0xFF contain 0x10), so it is polled on every interrupt and
    discarded. Native low bit 6 (timer 2), and native high bits 4/5 (payloads
    0x1DA/0x1DB, i.e. the guest's 0x15E/0x15F divider that the adapter routes to
    native bank-0 0x23/0x24 instead), have no guest counterpart to drive. Leave
    them unmapped. The "leading interrupt-model candidate" is void.
  - **New and decisive: the ISR calls every handler with `r0 = 0`.**
    `MaDevDrv_IntHandler` (`0x011C6580`) dispatches with
    `__call_via_r1(0, handler)` (`C100XEDK1.c:209954`), so the bit-7 handler is
    entered as `MaDevDrv_Fifo(0)`, **not** `MaDevDrv_Fifo(2)`. That is the missing
    link from the Fifo analysis above: the explicit caller
    (`MaSndDrv_ControlSequencer` case 1) passes `*DAT_01246ea0 == 2`, but the
    interrupt path passes 0, and only `param_1 == 0` satisfies the
    `local_18 == 0` guard at `C100XEDK1.c:210719` that steps `+0x28` 1 -> 2. So
    the end-of-sequence stop is reached through the FIFO interrupt:
    generator dry -> `MaSound_ReceiveMessage(0,0,0x80) == 0x7f` ->
    `MaDevDrv_EndOfSequence` sets `+0x28 = 1` -> next FIFO IRQ sets `+0x28 = 2` ->
    next FIFO IRQ runs `MaDevDrv_StopSequencer(0,1)` -> mask 0x9F -> silence;
    the matching `StartSequencer` restores 0xFF. The ISR acks by writing
    `uVar1 & 0x7f` to the data register, plus a trailing `0x80` after the bit-7
    handler, which our ack path already reproduces.

Still open, in ranked order: (1) **why the generator runs dry** -- the silence is
now fully explained downstream, so the remaining work is `MaSndDrv_UpdateSequence`
(`C100XEDK1.c:318574`), whose generator callback is `*DAT_01243fb4` and which
calls `MaSound_ReceiveMessage(0,0,0x80)` when it produces nothing; that message
decrements a block/loop counter at `*(ushort*)(DAT_011c81a4 + iVar6*2)` with
`iVar6 = *(int*)(DAT_011c81a4 + 0x68)` for channel 0 and sentinel `DAT_011c81a8`,
and returns `0x7f` (end) when it hits the sentinel or reaches zero. If that
counter starts at the wrong value, the tune reports end-of-sequence early;
(2) the paced parameter scan during a silence is `MaSndDrv_ControlSequencer(0,0)`
itself -- the `uVar3 * 6 + 5` loop over the 32 active voices in
`DAT_01246ac4 + 0x14`, i.e. a per-voice mute sweep, not a wait; (3) the cursor
ladder's bias/unit (now believed correct); (4) the dropped MA-3 registers
`0x146/0x147` plus `0x157/0x158/0x159/0x163`.

### Guest-state watch (2026-10-13; closes the IRQ question)

**The `+0x28` end-of-sequence chain is UNVERIFIED and must not be assumed.** A
frontend capture (Menu > 3 Ring Tone, Peer Gynt) recorded 15 guest rewrites of the
0x162 interrupt mask -- alternating 0x9F/0xFF, i.e. `MaDevDrv_StopSequencer` /
`MaDevDrv_StartSequencer` -- while the six watched bytes at `0x00407F44+0x21..+0x28`
**never moved at all**: `req=0 pulse=4 run=7 irq=0x09 en=0x0a eos=0x04 ctr=0000`,
identical in every run, with `irq`/`en`/`eos` never leaving their static
initialisers. Those bytes are the static `MaDevDrv_InitRegisters` selector table
(the linker map puts `index@..._0` at +0x21 and `data@..._1` at +0x30), so they are
*not* the runtime sequence state. Two consequences:

- `eos` never reaching 1 or 2 refutes the "EndOfSequence sets +0x28 = 1, the next
  FIFO IRQ sets it to 2, the one after runs StopSequencer" chain the earlier text
  assumed.
- `irq` (+0x25) never changing means **the guest serviced no MA-3 interrupt during
  that run**, which is itself suspicious and needs a longer capture.

Note also that the capture spanned only ~0.5 s and the mask pairs were *rapid*
(five of seven 0x9F -> 0xFF restarts within ~1 ms): routine sequencer start/preview
churn, not the multi-second stall. `MaSndDrv_ControlSequencer(ch,0)` is a stop plus
a **32-voice mute sweep** writing field-5 registers (5,11,...,125), and a following
`StartSequencer` only re-enables the interrupt mask -- it does not re-arm the
voices. That is now the leading hypothesis for the sustained silence: the voices
are muted and never re-programmed.

**RAM map** (derived from ROM, not guessed). The sound driver's pointer globals
`DAT_011c6874`, `DAT_011c7814`, `DAT_011c7bf4` and `DAT_011c7d74` all hold
`0x00407F44` (`C100XEDK1_1.bin` at `0x1C6874`, `0x1C7814`, `0x1C7BF4`, `0x1C7D74`),
so all four alias one struct. The linker map agrees:

| address | symbol |
|---|---|
| `0x00407F44 + bit*4` | 8 IRQ handler slots, written by `MaDevDrv_AddIntFunc` |
| `0x00407F4E` (+0x0A) | `ma_int_priority`, the ISR's 8-byte dispatch-order table |
| `0x00407F65` (+0x21, 15 B) | `index@MaDevDrv_InitRegisters_0` (static) |
| `0x00407F74` (+0x30, 15 B) | `data@MaDevDrv_InitRegisters_1` (static) |

The ISR dispatch byte is *proved* to be `*DAT_011c6874 + 0x0A + n`: the ROM bytes at
`0x011C687E` are Thumb code (`C0 46 78 1E ...`), not a table, so Ghidra's
`(int)DAT_011c6874 + uVar3 + 10` cannot be a pointer plus offset into ROM. The base
itself was also proved empirically -- the first watch transitions land at tick
~18 200 (18 us, the `.data` copy) and put the selector table at `+0x21` with values
`00 04 07 .. 09 0a .. 04`, exactly the linker-map offsets. The MMF player struct
resolves the same way: `DAT_011c81a4` = `0x00784158`, channel-0 index at +0x68, the
`MaSound_ReceiveMessage(0,0,0x80)` block/loop counter at `0x00784158 + index*2`,
sentinel `DAT_011c81a8` = `0xFFFF`.

**Preferred diagnostic (finds the real state by observation).** Because the six
named offsets are a trap, this instrument does not name offsets at all:
`MA3_ACCESS_WATCH=<start>,<end>[;<start>,<end>...]` (`src/gui.c`, and `src/main.c`
headlessly) arms the bus watch already used by `--mmio-trace` over arbitrary guest
ranges, **writes only** (the `watch_writes_only` flag on `Bus`), logging every
write with the guest PC to `build/gui-ram-watch.log`. Up to `BUS_MAX_WATCH` (8)
ranges can be armed at once, so the MA-3 driver block, the sequencer block and the
MMF player block can be correlated on a single tick timeline:

```
<tick> PC=011c7590 W4 00407f6c value=00000001 RAM
```

`python tools\ram_watch_report.py build\gui-ram-watch.log` then resolves each PC
against the firmware linker listing (`C100XEDK1.lst`; 16 292 code symbols) and
prints, per address, the writing function and offset plus the first/last tick and
the value sequence -- e.g. `MaDevDrv_Fifo+0x42` writing `0x00407F6C`. This both
locates the runtime state bytes (only the ones that change) and attributes each
change to an exact firmware routine, with no per-instruction cost, so real-time
audio is undisturbed.

The older per-instruction `MA3_SNDWATCH=<path>` watch (`src/ma3.c`; `1` selects
`build/ma3-sndwatch.log`) remains for cheap correlation of the six low struct bytes
with the 0x162 mask; decode with
`python tools\snd_watch_report.py build\ma3-sndwatch.log 1000000000`. It reads
through the side-effect-free `bus_peek` (`src/bus.c`), so it never faults, never
writes the access log and never touches a device. Its `eos`/`en` labels are known
to be misnamed -- the report tool prints those columns in *decimal*, so `en 10` is
`0x0a`. The tick base is nanoseconds when the profile has a clock attached (C100),
so the time column is real emulated time and the watch lines share their tick base
with the MA-3 traffic trace.

**Capture recipe for the ringtone.** In the GUI, before playing:
`$env:MA3_ACCESS_WATCH="0x00407F44,0x00408000"`, then Menu > 3 (Ring Tone) and let
Peer Gynt play **through at least one full silence** (~20 s), then **close the
window normally** so the CRT flushes the log -- do not kill the process, and do not
add `setvbuf` (MSVC's CRT fast-fails on `_IOLBF` with size 0, which is why this arm
does not call it). The first capture was too short (0.5 s) and may not have included
playback at all, which is why `irq` never moved.

**RAM watch result (2026-10-13, `MA3_ACCESS_WATCH=0x007A8000,0x007AC000`).** The
window is the right one: the ROM globals resolve into it and it contains the MMF
parser, the sequence state block and the channel array. 144 703 writes over ~351M
bus ticks, every one attributed to a named routine (`get_flex3`, `Decode_byte`,
`CastAwayNByte`, `Decode_byte7bit`, `MakeDeltaTime`, `SendDelayedPacket`,
`MaSndDrv_UpdateSequence`, `MaSndDrv_NoteOn/NoteOff/AllSoundOff/UserEvent/
ControlSequencer`, `Ma3_memcpy`). The other ROM globals resolve to `DAT_011c81a4`
-> `0x00784158` (MMF player block) and `DAT_01246ad0` -> `0x014BF9E0` (ROM packet
templates).

Decoded layout of the sequence state block at `0x007AB53C` (`DAT_01243fb0` and
`DAT_01246ea0` both hold this address):

| offset | address | role | setter |
|---|---|---|---|
| +0x00 | `0x007AB53C` | sequence-active byte, `*DAT_01246ea0` | 2 by `MaSndDrv_Create+0x758` / `ControlSequencer+0x384`; 0 by `ControlSequencer+0x26e` |
| +0x04 | `0x007AB540` | staging buffer pointer | `MaSndDrv_UpdateSequence+0x2e`, `SendDelayedPacket+0x68` |
| +0x08 | `0x007AB544` | staging length (0x80) | `UpdateSequence+0x32`, `SendDelayedPacket+0x70` |
| +0x0C | `0x007AB548` | **staging armed flag** | 1 by `SendDelayedPacket+0x2e`; 0 by `UpdateSequence+0xba` |
| +0x14 | `0x007AB550` | staging state (4/8) | `SendDelayedPacket+0x3e` |
| +0x18 | `0x007AB554` | value `UpdateSequence` returns | 0 by `UpdateSequence+0xc4` |

`SendDelayedPacket` is the **producer** (arms +0x0C, stages +0x04/+0x08);
`MaSndDrv_UpdateSequence` is the **consumer** (copies when +0x0C==1, else sets
+0x18=0 and produces nothing -- the "runs dry" branch).

**The dry condition, measured.** In the 46.5M-126.5M tick plateau the *only* writes
in the sequence block are 656 calls of `MaSndDrv_UpdateSequence` writing +0x04/+0x08/
+0x18, every one with +0x18 = 0, i.e. the "nothing staged" branch. No parser
(`get_flex3`), no notes, no `SendDelayedPacket`, and the MMF cursor at `0x007A8340`
never advances. The largest silence (9.81M ticks, 44 680 430 -> 54 491 688) is
preceded by `MaSndDrv_AllSoundOff`, which sweeps the channel array writes 0
(`0x007A8EBC`..`0x007A8F02`, stride 0x0A), then one `SendDelayedPacket`, then
nothing; the next write is an empty `UpdateSequence` pump.

So the generator is dry because the **producer side is idle**, not because of a
runaway FIFO: the pump keeps being called and keeps finding nothing staged.

**Correction (2026-10-13, second capture).** That idle window is *not* a stall. A
longer capture over the same session (three ranges: `0x00407F44` driver block,
`0x00784100` MMF player block, `0x007AB53C` sequence block; 58 410 writes over
`18162..870304053`) shows that `0x0078415A` is the preview's **loop/iteration
counter** (16-bit, written by `PC=011c87f6` to set, `PC=011c7e4c` to decrement,
`PC=011c854c`/`011c7e6a` to write the `0xFFFF` sentinel):

| tick | writer | value | meaning |
|---|---|---|---|
| 12 053 045 | `011c854c` | `0xFFFF` | init sentinel |
| 12 072 420 | `011c87f6` | `0x0001` | single-iteration preview |
| 44 685 317 | `011c7e4c` | `0x0000` | iteration exhausted |
| 44 685 332 | `011c7e6a` | `0xFFFF` | back to sentinel -- preview ended |
| 133 681 011 | `011c87f6` | `0x00FF` | new preview, 255 iterations |
| 140 399 065 / 344 327 783 / 526 360 360 / 708 408 582 | `011c7e4c` | `FE` / `FE` / `FD` / `FC` | one iteration each |

The 44 685 317 sentinel lands *exactly* on `MaSndDrv_AllSoundOff` (44 674 017) and
the start of the silence (44 680 430): **the first silence is a legitimate end of a
one-iteration preview**, and the 44.68M->133.68M gap is simply the interval before
the next preview starts. During the 255-iteration preview the staging/parse path
arms continuously (40-67 writes per 20M-tick bin, no gap) from 133.68M to 870M, and
the counter decrements through four full iterations of ~180-250M ticks each
(~47-64 s). So the guest-side loop is healthy in this capture; a loss of audible
sound while the guest keeps staging cannot be explained by this RAM state and must
lie downstream in the stream/DLL path.

**The driver block is boot-only.** Range A (`0x00407F44`..`0x00407FA0`) receives
exactly 23 writes, all at ticks 18 162-18 250 from `PC=0100044c` (the `.data`
scatter copy), and *nothing during playback*. The mask churn the guest-state watch
reports is a write to the MA-3 hardware port, not to this RAM block, so this block
holds only static init tables and cannot expose end-of-sequence state.

**The native FIFO feed differs from SMW5's.** `tools/fifo_diff.py` parses two
feeds (`WST <reg>` selects a destination, following `WDT <byte>` lines are data)
and compares, per register, op count, payload bytes and a payload sha1. Both the
capture proxy (`proxy/ma5hw_proxy.c`) and the emulator (`MA5HW_TRACE`,
`src/ma5_hw.c:144`) log the *same* native layer for the *same* `M5_EmuHw.dll`, and
both Peer Gynt captures span ~20 s of the file (reference ~960x960 frames from 15
`GEN` markers, emulator ~10176x96 frames from 159), so byte totals are
comparable:

| feed | selects | payload bytes | reg 01 (delayed) | reg 02 (immediate/RAM) |
|---|---|---|---|---|
| SMW5 `build/smw5cap/peergynt.log` | 662 | 21206 | 18075 | 2812 |
| emulator `build/pgynt-fifo.log` | 6619 | 24349 | 6683 | 15805 |

Same file, same duration, same DLL -- but the emulator issues 10x more selects
(~3.7 B each vs ~32 B) and splits its data the other way: SMW5 sends ~87% through
the timed delayed path, the emulator only ~30%, pushing the bulk through the
immediate/RAM register instead. This is the first concrete, quantified divergence;
whether it is the defect or a legitimate consequence of the MA-3 delayed framing
is the open question. The confirming test is an Arabesque A/B -- no emulator-side
Arabesque FIFO log exists yet (`build/arabesque-fifo.log` does not exist), so one
has to be captured with `MA5HW_TRACE` first.

Tools: `tools/fifo_diff.py` (native FIFO feed diff),
`tools/ram_watch_report.py` (aggregate, timeline, largest gaps),
`tools/ram_watch_windows.py` (per-tick-window writer attribution),
`tools/ram_watch_gap.py` (events leading into the largest silence).

Investigation tools added under `tools\`: `snd_watch_report.py` (decodes the
guest-state watch), `cursor_timeline2.py` (true live-cursor
reads by decoding status-3 packets), `cursor_ladder.py` (decodes `MA3_CURSOR_TRACE`
readbacks into the exact `uVar6`/target ladder `MaDevDrv_StreamUpdate` sees),
`param_reads.py` (reconstructed parameter
reads), `window_report.py` (access/selector mix and native status histogram for a
tick window), `irq_gaps.py` (accepted-IRQ interval histogram), plus
`cursor_gaps.py` and `cursor_timeline.py`. The replay trace also emits a
`-------- IRQ` marker per accepted interrupt, a `-------- IRQMASK value=`
line per 0x162 write, a `-------- CURSOR` line per
readback with `MA3_CURSOR_TRACE=1`, a `MA3_MASK_FIFO_EMPTY` status diagnostic
(negative result: it changes every status byte but not one AUDIO marker),
`MA5HW_STATE`'s per-voice decoder dump, and `MA3_SNDWATCH`'s guest-state
transitions (see the guest-state watch section above).

### ADPCM interpolation seam correction (2026-10-14)

The former dirty-ring lookahead workaround held the last decoded sample until
wrap. In Arabesque's opening stream, the ring wraps every ~248.25 ms and the
held interval lasts six 48-kHz output samples. The second wrap (~496 ms into the
stream) produces a particularly noticeable step. This is an adapter cache seam,
not six missing elapsed samples, and must not be hidden with global smoothing.

The adapter now computes the first new low nibble using the final old-buffer
predictor/step, installs it as interpolation lookahead, and stages its resulting
history. The native old decode index/history remain intact until actual wrap;
then index 1 plus the staged history are committed. This prevents decoding the
opening twice and prevents re-decoding the old tail with new history. Nonzero
static loop offsets retain the earlier fallback; the C100 stream ring uses zero.
The single-nibble arithmetic matches the pinned DLL decoder at `1001BCE4`.

A firmware Arabesque A/B changes only six samples per opening-ring seam, with
no shifted output timeline. At the prominent 496-ms seam, the largest PCM delta
falls from 5,347 to 3,154 and the maximum locally aligned Yamaha-reference
residual from 5,893 to 2,417. Other source-waveform transitions remain intact.
The ~64-ms pause at the end of the opening is also present in the SMW5 reference
(~71 ms there); it is not evidence of a lost stream refill.

CPU tones now mix into Yamaha's existing output blocks/queue when Yamaha is
active. Bumer's song output after CPU tones settle is bit-exact across this
routing change. A 26.6-second idle-host mixed playback had zero output underruns;
a run competing with a full build recorded one. Traffic tracing remains an
optional performance cost. Listener confirmation of the remaining audible clicks
is still needed; these measurements do not prove every MMF is gap-free.
