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
(`0x012AF30C`) programs tone state. That path is not implemented yet and is
not replaced with host keypress beeps.

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
This reduces scheduling overhead; eliminating host underruns and completing
instrument/control compatibility are not claimed.

The isolated firmware-driven Arabesque run completes without CPU/audio errors
and exercises eight ADPCM cache refreshes. The opening sample is still loaded
on the first start, and the guest continues its ring-buffer refills. The user
has not yet confirmed this build's audible stream/instrument behavior.
