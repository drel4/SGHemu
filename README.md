# SGHemu

Samsung SGH feature-phone emulator, starting with SGH-C100 / Skyworks CX805.
Runtime and tools are plain **C17**, MSVC **2026 x86**, `/W4 /WX`.
Release uses `/O2 /GL /LTCG /MT`; Debug enables `/RTC1`.
Windows 7 API/subsystem target; actual execution on Windows 7 is untested.

[Download Windows x86 builds](https://github.com/drel4/SGHemu/releases).

> **Firmware compatibility:** most functionality is currently available for
> **C100XEDK1**. C100XEDA2 and C100XEDD1 have more limited bring-up support;
> reaching standby does not mean they support the same features. SGHemu is
> designed for multiple models, but other SGH models are not supported yet.

## Phone output and appearance

XEDK1 navigation/key tones now have an independent CPU/DSP PCM output, even
without Yamaha DLLs. The program icon shows the native RGB service-LED color.
**L&F -> LCD ghosting** toggles a saved LCD response effect;
it does not alter guest pixels or screenshots. See
[implementation and verification](docs/c100-phone-output.md).

**L&F -> C100 MA detune (-12.5 cents)** optionally lowers Yamaha output pitch
to approximate the reported C100 detuning. It is off by default and saved;
CPU navigation tones are unchanged. This clock-style effect also lengthens MA
playback by about 0.72%; the exact hardware offset is not independently verified.

**File -> Logging** toggles `build/gui-unmapped.log`,
`build/gui-ma3-traffic.log` and `build/gui-profile.log` while running, without resetting the phone.
Logging is off by default, saved under `[diagnostics] logging` in
`state/emulator.ini`, and appends across toggles/resets. The old audio-only
`[audio] trace` setting no longer enables GUI logging. Performance profiling is
also off when logging is off; see [reading performance reports](docs/performance-profiling.md).

## AI-assisted project

This project is AI-assisted. If you hate AI-assisted software, don't use it.
This notice is not an additional restriction on the WTFPL license.

## Current milestone

The C100 firmware now reaches **normal standby with a virtual SIM**, showing
wallpaper and the Menu / Name soft keys. XEDK1 also has an experimental virtual
carrier for calls and SMS; other profiles still show **No Service**.
Pixels come from the guest's x16 LCD writes, not an extracted asset or host UI.
C100XEDA2, C100XEDD1 and C100XEDK1 have all passed SIM initialization and
standby captures. This is experimental hardware bring-up, not a complete phone:
unknown MMIO still continues and overall timing remains provisional, although
the clock tree uses firmware-derived clock modes. Experimental MA-3
translation now renders guest startup audio through the MA-5 hardware DLL;
volume/configuration compatibility remains incomplete. The guest keypad matrix is connected. The default strict profile still stops on unknown
hardware; folder import selects the documented bring-up profile.

Implemented: shared ARM7TDMI ARM/Thumb interpreter ([optimization and checks](docs/arm7-optimization.md)), ROM/RAM/config loader,
flash command/status handling, interrupt routing with a diagnostic frame source,
LCD command/data ports, and the observed PMIC serial-pin protocol. See
[LCD/boot evidence](docs/c100-lcd.md) and [memory map](docs/c100-memory-map.md).
The C100XEDK1 profile now uses GPIO registers for charger detection and service
LED output, plus an ARM/DSP ADC mailbox subset for battery measurements; these
paths no longer depend on firmware function addresses. Native firmware handles
battery conversion/filtering and charging animation. This is not a full DSP/RF
implementation. Navigation tones, virtual cellular/WAP and content-import helpers
still have firmware-specific bindings. See [battery support](docs/c100-battery.md)
and [ADC mailbox](docs/cx805-adc-mailbox.md).
The bring-up frontend now provisions missing NVM using the firmware's own
factory-sector formatter and initialization-marker writer, saves it, and cold
resets. Normal startup then creates defaults and initializes the virtual SIM.
Removing the virtual card restores **Insert SIM Card**. No physical GSM radio is
emulated; the XEDK1 virtual carrier can provide local service, while unbound
profiles still show **No Service**. See [SIM implementation and evidence](docs/c100-sim.md).
Existing saves load automatically without formatting. This uses a narrowly
verified internal-ROM version query, not fabricated BIST results. See
[internal-ROM analysis and UI evidence](docs/c100-irom.md).
The post-splash NVM startup failure and IRQ/flash fixes are recorded in
[startup tracing](docs/c100-post-splash.md).
The [persistent-storage investigation](docs/c100-nvm.md) distinguishes UI
records, blank-flash initialization and the reported 300 KB user quota.

## Build and run

```powershell
.\build.cmd release
.\build\sghemu-gui.exe
```

For an optimized x86 build with the C runtime linked statically, run
`build.cmd release`. Both build modes run the synthetic verification suite.
Release ZIPs contain the GUI, CLI, firmware-inspection and standalone audio
tools, model configs, and documentation. Extract the whole archive and launch
`build/sghemu-gui.exe`; keep the supplied directory layout. Firmware and Yamaha
DLLs must be supplied separately. Hardware audio is opt-in; v0.1.0 was silent.
v0.2.1 improves sequencer restart timing and v0.2.2 adds an experimental ADPCM
stream-cache correction. Instrument compatibility and occasional stutter remain
under investigation; real-handset tempo and complete stream playback are unverified.

In the GUI use **File > Open firmware folder (Ctrl+O)**. Pick the folder containing
both firmware files, or a parent folder containing several sets. Exact SHA-256
hashes identify the model/version even when files have been renamed. A complete
match boots automatically; multiple matches offer a version selection. No guessed
mapping is used for unknown firmware. `.lst` files alone cannot boot.

The native window contains only the LCD, native keypad buttons beneath it, and
the top menu. F5 runs, F6 pauses, F10 steps, Ctrl+R resets (paused). State and
hardware counters are in View > Diagnostics. Buttons drive the firmware-scanned
keypad matrix. See [GUI controls](docs/native-gui.md) and [keypad mapping](docs/c100-keypad.md).
**Emulation > Keyboard text input (abc/ABC)**, or **Ctrl+T**, lets you type English
letters, spaces and periods directly while a phone text field is active. Select
the phone's English **abc/ABC** multi-tap mode (not T9) first; case follows the
phone's selected mode. Each character generates timed numeric keypad taps followed
by Right, so consecutive letters on the same key do not require waiting. Numbers
remain direct phone keypad presses. Backspace deletes; Esc cancels queued typing.
Queued text is discarded on focus loss, pause or reset. The toggle is saved;
turn it off to restore Space as the phone's OK button.
**Emulation > Virtual SIM inserted (reset)** toggles the card with a cold reset.
**Emulation > Import Yamaha hardware DLL...** enables experimental audio;
select the pinned `M5_EmuHw.dll`, then press F5. Settings live in `state/audio.ini`.
Its synthetic identity is configured in `models/c100/sim/test-sim.cfg`.
NVM automatically loads/saves under ignored `models/<model>/state/`, separated by firmware
hashes and memory map. Valid legacy `state/` saves migrate automatically without deleting the originals.
No manual Save command is needed. First boot takes
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
Diagnostic logs and captures go into ignored `build/`. GUI logging is disabled
by default and appends when enabled; CLI diagnostic runs use their own logs. Flash changes affect host-owned emulated memory only, not ROM files.

## Verification

`build.cmd release` runs synthetic and differential checks covering ARM/Thumb, memory and
peripherals, firmware detection, NVM, keypad/SIM, MA-3, content/JAR imports,
RTC, CPU tone/LED mixing, GPIO/ADC mailbox, decode-cache equivalence, peripheral
deadline scheduling and ADPCM lookahead. Synthetic tests require no
proprietary inputs. Optional real-ROM
regressions, compiled by the same build:

```powershell
.\build\c100_boot_test.exe models/c100/c100xeda2-bringup.cfg
.\build\c100_boot_test.exe models/c100/c100xedd1-bringup.cfg
.\build\c100_boot_test.exe models/c100/c100xedk1-bringup.cfg
.\build\c100_identity_test.exe
```

## Obtaining firmware

You can find firmware [here](https://t.me/samsfunclub/6/2543).
Download and extract the ROM files yourself, then choose
**File -> Open firmware folder...** and select the folder containing both BIN
images. The emulator detects supported C100 firmware sets by their hashes.
ROMs are not included in SGHemu releases.

## MA audio

For MA-3 ringtone/startup sound, download the archive
[here](https://3.onj.me/phonetones/Software%20and%20Soundbanks/Software/Yamaha%20SMAF/SMAF_newer_DLLs.zip)
and extract **only `M5_EmuHw.dll`**. In SGHemu, choose
**Emulation -> Import Yamaha hardware DLL...**, select that file, then run the
phone. The emulator does **not** require `M5_EmuSmw5.dll` or a separate voice bank.
The selected DLL must match the supported hash/version. DLLs are not bundled.

### Audio implementation and standalone diagnostics

MA-5 standalone native file playback with embedded FM voices has been proved;
`__cdecl` was verified in Ghidra and through 1,000,000 calls without ESP drift.
The user-supplied updated software 2.2.0.0 / hardware 1.6.0.0 DLL pair is hash
pinned; other binaries are rejected. See [audio evidence](docs/ma5-audio.md) and
`MA5libs/doc/README.md` for API details.

```powershell
.\build\fwinspect.exe models/c100/roms/C100XEDA2/C100XEDA2_1.bin --extract-mmf 0x418cce build/c100-embedded-voice.mmf
.\build\ma5probe.exe MA5libs build/c100-embedded-voice.mmf 10 1000000
```

The emulator's experimental hardware route consumes the guest's timed FIFO
packets, translates MA-3 parameter addresses and generates stereo PCM through
`Hw_Generate`, with chip IRQs routed back to the firmware. Startup output has
been captured on all three C100 versions. All DLL calls and playback buffers
belong to one audio thread. Volume mapping and some chip controls are provisional.
PCM/ADPCM stream interrupts, refill timing and sample-position reads are now
translated, but compatibility remains incomplete. The phone filesystem manager
supports directory inspection, TSV export, NVM backup and content import on
C100 XEDK1. **File -> Phone filesystem...** opens a two-pane manager: host import
queue on the left, phone files on the right. **Add files...** accepts sounds, PNG/JPEG/GIF/BMP pictures and
CLDC 1.0 / MIDP 1.0 JARs. Pictures are fitted to 128 x 128 (first frame only).
Use **Import queue >>** for bulk loading; each item is saved before the next
starts. JARs run through the phone's native installer. **Java games** reboots into the native MIDlet menu using the
saved NVM. Other firmware profiles do not yet declare these installer entries.
The handset's own Java menu is WAP & Games -> Downloads -> Games and more.
For larger apps, enable **Emulation -> Extended Java buffers (next import/reset)**
before importing again. This optional compatibility mode enlarges the native
installer's working buffers to 1 MiB each; this is **not original hardware behavior**.
It is off by default and does not replace the native installer or VM. Compilation
can take minutes. A MIDP 1.0 manifest alone does not guarantee an app will run.
The **Emulation** menu also provides **High process priority** (off by default)
and **Sync phone date/time to Windows** (on by default for C100 bring-up profiles).
Priority changes take effect immediately; switching it off restores Normal.
Host sync uses Windows local time, including its timezone/DST settings. Switching
sync off allows handset clock changes and advances the RTC with emulated time.
These preferences persist in `state/emulator.ini`; phone NVM stays model-local.
CLI equivalents: `--high-priority` and `--rtc-sync` (both opt-in on the CLI).

**Emulation -> Cellular** (XEDK1) provides an enabled toggle, a custom network
name (default **Tele2**), **Send SMS to phone...** and **Call phone...**. Dialing
from the handset opens a host **Incoming call from phone** window with
**Accept / Decline**, then **Hang up** when connected. These are local simulated
calls/messages, not connections to a real operator; voice-call audio is not
implemented. Incoming SMS can be read and replied to in the native phone menus.
SIM contacts, SMS and message parameters are automatically saved beside each
firmware's NVM as `.snv.sim` and restored on reset/startup.

**Cellular -> GSM** selects **Off**, **Internet**, or **Internet + built-in WAP
gateway** (packet data only). Selecting the built-in gateway shows setup
instructions: gateway IP **10.0.0.1**, non-secure WAP, port **9201**; test homepage
**http://sghemu.local/**. Configure these in the phone's WAP browser profile.
The C gateway uses Windows HTTP/HTTPS fetching and a small text/link conversion
for old WAP browsers. This is experimental WAP/UDP transport, not complete GSM,
Java TCP networking, WTLS or a full HTML browser. See
[cellular and SIM details](docs/c100-cellular.md).

The GUI remembers the last successfully opened firmware/model and automatically
loads and runs it on subsequent launches. **Emulation -> Battery** (XEDK1) offers
an editable 0-100% level and a charging-cable toggle, persisted in
`state/emulator.ini`. These drive native battery/charger callbacks; the voltage
curve is provisional, not a calibrated hardware battery model.
CPU/DSP navigation tones are implemented separately from MA synthesis. See
[content import details](docs/c100-content-import.md).
CLI: `--ma5-hw PATH`, optional `--audio-wav FILE` and `--audio-ticks-hz N`.
The tick rate is not a measured CPU clock. DLL download requires explicit consent;
no firmware, DLL, voice bank or generated proprietary asset is distributed.

CLI Java examples (use your own game file):

```powershell
.\build\sghemu.exe models/c100/c100xedk1-bringup.cfg --import GAME.jar --lcd-out build/installed.bmp
.\build\sghemu.exe models/c100/c100xedk1-bringup.cfg --java-menu --lcd-out build/java-menu.bmp
.\build\jarinspect.exe GAME.jar
```

The manifest check is not an API-compatibility guarantee. Games requiring
MIDP 2.x or APIs absent from the original handset may not run. The JAR manifest
decoder uses Mark Adler's [puff](third_party/puff/README.md), with its original
license preserved separately from the project's WTFPL.

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
