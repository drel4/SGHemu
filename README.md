# SGHemu

Samsung SGH feature-phone emulator, starting with SGH-C100 / Skyworks CX805.
Runtime and tools are plain **C17**, MSVC **2026 x86**, `/W4 /WX /RTC1`.
Windows 7 API/subsystem target; actual execution on Windows 7 is untested.

[Download Windows x86 builds](https://github.com/drel4/SGHemu/releases).

## AI-assisted project

This project is AI-assisted. If you hate AI-assisted software, don't use it.
This notice is not an additional restriction on the WTFPL license.

## Current milestone

The C100 firmware now reaches **normal standby with a virtual SIM**, showing
**No Service**, wallpaper, and the Menu / Name soft keys.
Pixels come from the guest's x16 LCD writes, not an extracted asset or host UI.
C100XEDA2, C100XEDD1 and C100XEDK1 have all passed SIM initialization and
standby captures. This is experimental hardware bring-up, not a complete phone:
unknown MMIO still continues, clocks are uncalibrated, and MA-3 audio is not
connected. The guest keypad matrix is connected. The default strict profile still stops on unknown
hardware; folder import selects the documented bring-up profile.

Implemented: shared ARM7TDMI ARM/Thumb interpreter, ROM/RAM/config loader,
flash command/status handling, interrupt routing with a diagnostic frame source,
LCD command/data ports, and the observed PMIC serial-pin protocol. See
[LCD/boot evidence](docs/c100-lcd.md) and [memory map](docs/c100-memory-map.md).
The bring-up frontend now provisions missing NVM using the firmware's own
factory-sector formatter and initialization-marker writer, saves it, and cold
resets. Normal startup then creates defaults and initializes the virtual SIM.
Removing the virtual card restores **Insert SIM Card**. No radio/network is
emulated; **No Service** is expected. See [SIM implementation and evidence](docs/c100-sim.md).
Existing saves load automatically without formatting. This uses a narrowly
verified internal-ROM version query, not fabricated BIST results. See
[internal-ROM analysis and UI evidence](docs/c100-irom.md).
The post-splash NVM startup failure and IRQ/flash fixes are recorded in
[startup tracing](docs/c100-post-splash.md).
The [persistent-storage investigation](docs/c100-nvm.md) distinguishes UI
records, blank-flash initialization and the reported 300 KB user quota.

## Build and run

```powershell
.\build.cmd
.\build\sghemu-gui.exe
```

For an optimized x86 build with the C runtime linked statically, run
`build.cmd release`. Both build modes run the synthetic verification suite.
Release ZIPs contain the GUI, CLI, firmware-inspection and standalone audio
tools, model configs, and documentation. Extract the whole archive and launch
`build/sghemu-gui.exe`; keep the supplied directory layout. Firmware and Yamaha
DLLs must be supplied separately. Emulator audio is currently silent.

In the GUI use **File > Open firmware folder (Ctrl+O)**. Pick the folder containing
both firmware files, or a parent folder containing several sets. Exact SHA-256
hashes identify the model/version even when files have been renamed. A complete
match boots automatically; multiple matches offer a version selection. No guessed
mapping is used for unknown firmware. `.lst` files alone cannot boot.

The native window contains only the LCD, native keypad buttons beneath it, and
the top menu. F5 runs, F6 pauses, F10 steps, Ctrl+R resets (paused). State and
hardware counters are in View > Diagnostics. Buttons drive the firmware-scanned
keypad matrix. See [GUI controls](docs/native-gui.md) and [keypad mapping](docs/c100-keypad.md).
**Emulation > Virtual SIM inserted (reset)** toggles the card with a cold reset.
Its synthetic identity is configured in `models/c100/sim/test-sim.cfg`.
NVM automatically loads/saves under ignored `state/`, separated by firmware
hashes and memory map. No manual Save command is needed. First boot takes
longer while the guest formats storage, then resets and boots normally.
File > Import NVM snapshot and reset remains for importing existing `.snv`
files; press F5 afterward. ROM files are never modified.

Firmware belongs in model-specific, ignored folders:

```text
models/c100/roms/C100XEDA2/C100XEDA2_1.bin
models/c100/roms/C100XEDA2/C100XEDA2_2.bin
models/c100/roms/C100XEDD1/...
models/c100/roms/C100XEDK1/...
```

Imported files may remain in their original folder; selecting it does not copy or
modify them. Relative ROM paths in configs are **config-directory relative**.
See [profiles and folder identification](docs/firmware-layout.md) for new models.

```powershell
.\build\sghemu.exe --folder models/c100/roms/C100XEDA2 10000000 --lcd-out build/c100-lcd.bmp
.\build\fwinspect.exe models/c100/roms/C100XEDA2/C100XEDA2_1.bin --strings
.\build\sghemu.exe models/c100/c100xeda2.cfg 100000 --trace
```

CLI with several detected versions selects the first and reports that choice.
`--trace`, `--mmio-trace`, `--until ADDRESS`, `--continue-unmapped` are available.
CLI persistence is on by default; `--state-dir DIRECTORY` selects a different
store, and `--no-persist` gives an isolated erased-storage diagnostic run.
`--nv-load`/`--nv-save` remain explicit diagnostic import/export options.
`--sim present|none` overrides card presence for that run.
Diagnostic logs and captures go into ignored `build/`; logs are overwritten on
reload/run. Flash changes affect host-owned emulated memory only, not ROM files.

## Verification

`build.cmd` runs 211 CPU/bus, 89 machine, 100 peripheral, 24 firmware-folder,
138 NVM snapshot/persistence, 43 keypad, 73 internal-ROM HLE and 135 SIM checks (**813 total**) using synthetic
fixtures (no proprietary inputs required). Optional real-ROM
regressions, compiled by the same build:

```powershell
.\build\c100_boot_test.exe models/c100/c100xeda2-bringup.cfg
.\build\c100_boot_test.exe models/c100/c100xedd1-bringup.cfg
.\build\c100_boot_test.exe models/c100/c100xedk1-bringup.cfg
.\build\c100_identity_test.exe
```

## Audio

MA-5 standalone native file playback with embedded FM voices has been proved;
`__cdecl` was verified in Ghidra and through 1,000,000 calls without ESP drift.
The user-supplied updated software 2.2.0.0 / hardware 1.6.0.0 DLL pair is hash
pinned; other binaries are rejected. See [audio evidence](docs/ma5-audio.md) and
`MA5libs/doc/README.md` for API details.

```powershell
.\build\fwinspect.exe models/c100/roms/C100XEDA2/C100XEDA2_1.bin --extract-mmf 0x418cce build/c100-embedded-voice.mmf
.\build\ma5probe.exe MA5libs build/c100-embedded-voice.mmf 10 1000000
```

The dedicated audio thread owns all DLL calls and playback buffers. The emulator
still uses the silent interface; the MA-3 FIFO/commands, duration interrupts and
clock synchronization remain future work. DLL download requires explicit consent;
no firmware, DLL, voice bank or generated proprietary asset is distributed.

## Layout

| Directory | Contents |
|---|---|
| `src/` | Shared CPU, bus, peripherals, native GUI, firmware loader, audio interface |
| `models/` | Model memory/peripheral configs and hash-identification descriptors |
| `tools/` | C firmware/audio tools and Ghidra Java analysis scripts |
| `tests/` | Instruction programs, protocol fixtures, optional real-ROM regressions |
| `docs/` | Reverse-engineering evidence and remaining hardware work |
| `models/**/roms/`, `MA5libs/*.dll`, `build/` | Ignored private inputs/artifacts |

## License

Copyright (c) 2026 drel4 and SGHemu contributors. SGHemu's original source code,
tools and binaries are licensed under the [WTFPL version 2](LICENSE).
This license does not apply to Samsung firmware, Yamaha libraries/voice banks,
or other third-party material, which is not included in releases.
