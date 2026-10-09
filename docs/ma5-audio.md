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

## Clock/integration implications

The native mode sequences and drives host output asynchronously. CPU
instruction stepping does not advance its sample clock. All SGHemu DLL calls
must remain on one dedicated audio thread. The standalone probe already does
that with `_beginthreadex`; the emulator currently selects a silent `AudioOps`
backend, with no guessed MA-3 register interface attached.

Future device work, in order:

- Trace the C100 MA-3 driver and log FIFO bytes/register control writes.
- Determine whether the FIFO carries a complete `MMMD` file. Do not feed
  unframed register-level commands to file mode.
- Buffer and validate SMAF big-endian total-size-plus-eight, using bounded
  accumulation and host ownership through complete teardown.
- Map verified start/stop/volume and loop bits. For a changed loop count,
  recreate the full object lifecycle; `Start` count is zero for forever,
  positive N for N passes as described in the supplied notes.
- Parse supported SMAF sequence formats and delta-time bases. Use that
  duration in **emulated time** to generate the device's completion status/
  interrupt. Unsupported duration formats must report unknown, not invent an
  end time. Gate playback status on finite versus infinite loops.
- Add pause/speed policy: native host sequencing will otherwise keep going
  while firmware emulation is paused or running faster/slower than real time.

There is no EOS callback assumption or wall-clock-generated guest interrupt.
The probe's seconds argument is a diagnostic stop deadline only.

## Distribution and acquisition

DLLs, MMF/voice files, firmware and captured WAVs are excluded by `.gitignore`.
The supplied directory contains DLLs and technical notes but no separate
redistribution license; no proprietary files are being distributed here.
The current CLI requires explicit manual import and a path. A future GUI may
offer the brief's download URL only after explicit user consent, extracting
only the two required DLLs and rejecting hashes not supported by the backend.
No download was requested or performed for this task.
