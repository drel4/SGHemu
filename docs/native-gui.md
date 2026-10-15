# Native Win32 frontend

Run `build/sghemu-gui.exe`. The main client area contains the unframed LCD,
native keypad buttons below it and a bottom status bar; the native title/menu
bar remains.

## Status bar and Water

The bottom native status bar shows Running, Paused, Stopped (including CPU halt)
or No firmware. Its right pane reports emulated nanoseconds / elapsed host time
as a percentage: 100% is real-time, not host CPU usage. Quarter-second samples
feed a rolling two-second average including audio pacing and host waits. Pause resets the window; a firmware
reset cannot produce a huge wrapped speed. Before the first sample, or without
a configured emulated clock, the running speed is `--%`. The GUI can refresh its
state even when the emulation mutex is busy, but never combines a stale guest
timestamp with the current wall time. After two seconds without a fresh snapshot
speed becomes unknown (`--%`); measured sustained no-progress becomes zero.

The window grows to accommodate the bar; it does not cover the keypad or LCD
at scales 1x, 2x or 3x.

**L&F -> Water (not accurate)** is an off-by-default, session-only fictional MA fault simulator,
not a physical model of water damage. The status bar also marks an active Water
session. No restart is needed to toggle it. It introduces bounded sequencer
divider changes (25..325% of the original divider) every 25..160 ms, occasional
voice pitch/level payload bit errors and PCM sample-hold/bit-crush/dropout/crackle
effects. PCM effects run on the MA worker
before CPU-tone mixing. Sample uploads also get sparse low-nibble bit flips
(about one byte in 512), plus occasional 12..43-byte corruption bursts, affecting
uploaded voice definitions and PCM/ADPCM data sent to the MA without changing
the guest's clean copy. Both immediate and delayed RAM uploads are covered;
delayed packet length bytes and voice selection/note-gate fields are preserved.
Packet framing, addresses, length fields, firmware bytes
and NVM are not modified by the effect. Disabling restores the
original divider and bypasses PCM processing; already-corrupted queued pitch
commands and damaged uploaded samples may persist until the ringtone is
reselected or the phone is reset.
Asynchronous clock faults restore the guest's selected MA port afterwards, so
they cannot redirect an in-progress firmware read or sample-RAM write into a
control register and leave playback stuck on held notes.
Clear/Back occupies a full-width row between navigation/call keys and digits.

The cellular **Send SMS to phone** window has an **SMS level** selector:
Normal (no class, the default), Class 0 (Flash), Class 1 (Phone), Class 2 (SIM),
and Class 3 (Terminal). This sets the actual incoming TP-DCS class while retaining
UCS-2 text encoding; the firmware decides how to display/store each class.
Encoding follows [3GPP TS 23.038 section 4](https://www.etsi.org/deliver/etsi_TS/123000_123099/123038/08.01.00_60/ts_123038v080100p.pdf).

Integer LCD zoom 1x/2x/3x uses GDI nearest-neighbor `StretchDIBits`.

File > Open firmware folder (Ctrl+O) identifies complete ROM pairs by SHA-256
and automatically boots the selected profile. Multiple supported sets offer a
selection menu. Unknown/incomplete sets report a clear message. File > Open
model config is retained as an advanced workflow. Config ROM paths are relative
to their config directory; the GUI locates its workspace relative to `build/`.

File > Phone filesystem manager opens a native directory inspector with Refresh,
Export directory (TSV metadata) and Back up NVM. File-content export and MMF,
picture/JAR imports are not implemented yet; see [filesystem evidence](c100-filesystem.md).

NVM automatically loads from ignored `models/<model>/state/` files keyed by firmware hashes
and memory map. It saves on pause, reset/model change, normal close, and changed
storage checkpoints every five seconds while running. Unchanged storage is not
rewritten. Manual Save has been removed. File > Import NVM snapshot and reset
loads an explicitly selected `.snv` into a fresh reset and commits it to the
automatic store; press F5 to run. The imported source is not overwritten.
Corrupt or concurrently locked saves block startup rather than being reformatted.

When no save exists, C100 bring-up runs the guest factory-sector formatter and
marker writer, commits the successful result, and cold-resets automatically.
The next boot initializes UI defaults; once the observed internal-ROM version
query marks completion of UI record loading, those defaults are saved and a
final cold reset boots from settled storage. This is explicit emulator provisioning
using guest service routines, not a claim that ordinary phone boot automatically
formats erased flash. Partial/failed provisioning is not committed.

C100 bring-up profiles insert a synthetic virtual SIM by default. **Emulation >
Virtual SIM inserted (reset)** saves phone NVM, cold-resets with the opposite
card-presence setting, and preserves whether execution was running or paused.
This is a reset-time switch, not hot insertion. The override remains in effect
for the current GUI session. Card identity comes from `sim/test-sim.cfg` in the
model folder. Card file changes are currently volatile; phone NVM remains
separately autosaved. See [SIM evidence and limitations](c100-sim.md).

| Control | Input |
|---|---|
| Run / Pause / Step | F5 / F6 / F10 |
| Reset/reload, paused | Ctrl+R |
| Toggle English multi-tap text input | Ctrl+T |
| Soft keys, Call / End | F1 / F2, F3 / F4 |
| Navigation / OK | Arrows / Enter or Space |
| Digits / Star / Hash / Back | Number keys, Shift+8, Shift+3, Backspace |

**Emulation > Keyboard text input (abc/ABC)** enables PC letters, spaces and
periods in a phone text field. First select the handset's English abc/ABC mode,
not predictive T9; capitalization follows that handset mode. Letters are queued
as real numeric keypad taps, then Right commits each character immediately.
For example, `abc` generates `2, Right, 2, 2, Right, 2, 2, 2, Right`.
Both press and release intervals are 60 ms of emulated time. Typing is asynchronous;
wait for the LCD to catch up, or press Enter/a softkey to queue it after the text.
Numbers still act as direct numeric keypad presses, not literal digit insertion.
Space becomes the phone's space key while this option is enabled; Enter remains OK.
Backspace queues Clear; Esc cancels pending input. Focus loss, menu entry, pause
and reset discard the queue and release all keys. The toggle is saved in
`state/emulator.ini`; turning it off restores the original direct controls.

Buttons track held/released state and release on focus loss/menu entry. The
host key state drives the guest-scanned matrix in C100 bring-up profiles. OK is
a convenience alias for the left soft key. See [keypad mapping](c100-keypad.md).
View >
Diagnostics exposes CPU, fault, ROM, LCD and input counters. The explicit LCD
test pattern is a separate rendering check and resets off on model reload;
it does not write guest pixels. Firmware boot snapshots use only guest pixels.

Firmware-generated standby output with the virtual SIM is now available for
XEDA2/XEDD1/XEDK1; without the card it shows Insert SIM Card. No Service is
expected because radio/network emulation is absent. The strict startup profile remains paused;
folder import chooses the experimental bring-up profile. No decorative frame,
labels, captions or status bar were added to the main client area.

Silent execution batches are limited to 120,000 instructions or approximately 8 ms
per 16 ms GUI timer callback. Hardware-audio execution allows up to 1,000,000
instructions for clock-enabled profiles, or one tenth of the legacy tick rate,
with a 32 ms wall-time budget; buffered WinMM output supplies backpressure. The audio worker requests
1 ms multimedia timer resolution only while speaker output is active, and releases
it on teardown. C100 frame IRQs and Yamaha synthesis use estimated core cycles
converted to emulated time, not Windows wall time. SIM/DMA retain legacy tick
timing, and external memory wait states remain uncalibrated. Firmware reports an
awake ARM clock of 23.4 MHz; see [clock evidence](c100-clock.md). Experimental hardware audio is
opt-in: **Emulation > Import Yamaha hardware DLL...** imports the pinned
`M5_EmuHw.dll`, resets the phone paused, and saves the path/enable setting in
`state/audio.ini`. Press F5 afterward. The audio toggle also resets paused.
All DLL calls run on a dedicated worker. F6 pauses both guest chip time and
WaveOut output. Diagnostics include FIFO/IRQ and PCM counters. Ten-millisecond
PCM blocks use eight output buffers; speaker pacing may extend an execution batch.
The optional INI `ticks_hz` applies only to profiles without a clock model;
it is interpreter ticks/sec, not physical CPU MHz.
See [audio evidence and limitations](ma5-audio.md).

LCD presentation prefers a target after LCD commands/writes have been quiet
for 3 ms of guest time. Normal brief clears and partial redraws therefore
stay out of the 75-ms response filter. Painting composes the background and LCD
offscreen and presents them together, with background erasure suppressed to avoid
flashy frames. Guest pixels and exported screenshots are unaffected.
Completed frames use a separate short handoff lock, so normal LCD updates and
ghosting blends continue on GUI timers even during CPU/audio mutex waits.
Continuously redrawing content has a 33-ms capture bound, so a game that never
leaves a 3-ms quiet gap cannot starve presentation. That fallback snapshots live
LCD memory atomically; it does not skip any guest drawing or CPU instructions.

L&F also provides the saved, default-off **C100 MA detune (-12.5 cents)** option.
It changes Yamaha output live without resetting the phone; other phone models
and CPU/navigation tones are unaffected.

Build target is MSVC 2026 x86 C17 with `/W4 /WX /RTC1` and Win7-compatible
Win32/GDI/common-control APIs. Common Controls v6/DPI manifest is embedded.
Windows 7 execution is untested. Paths not representable in the current ANSI
code page are explicitly rejected by the folder dialog rather than corrupted.
