# Native Win32 frontend

Run `build/sghemu-gui.exe`. The main client area contains only the unframed LCD
and native keypad buttons below it; the native title/menu bar remains. Integer
LCD zoom 1x/2x/3x uses GDI nearest-neighbor `StretchDIBits`.

File > Open firmware folder (Ctrl+O) identifies complete ROM pairs by SHA-256
and automatically boots the selected profile. Multiple supported sets offer a
selection menu. Unknown/incomplete sets report a clear message. File > Open
model config is retained as an advanced workflow. Config ROM paths are relative
to their config directory; the GUI locates its workspace relative to `build/`.

NVM automatically loads from ignored `state/` files keyed by firmware hashes
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
| Soft keys, Call / End | F1 / F2, F3 / F4 |
| Navigation / OK | Arrows / Enter or Space |
| Digits / Star / Hash / Back | Number keys, Shift+8, Shift+3, Backspace |

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

Execution batches are limited to 40,000 instructions or approximately 8 ms
per 16 ms GUI timer callback. Guest IRQs are driven by interpreter ticks, not
Windows wall time; clock speed remains unknown. All MA-5 calls remain in the
standalone audio probe's dedicated thread, and emulator audio remains silent.

Build target is MSVC 2026 x86 C17 with `/W4 /WX /RTC1` and Win7-compatible
Win32/GDI/common-control APIs. Common Controls v6/DPI manifest is embedded.
Windows 7 execution is untested. Paths not representable in the current ANSI
code page are explicitly rejected by the folder dialog rather than corrupted.
