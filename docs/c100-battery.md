# C100 battery and charger simulation

Implemented for XEDK1 only; other model profiles do not advertise the menu
until their guest entry points have been verified.

The native **Emulation -> Battery** submenu accepts a percentage from 0 to 100
and toggles the charging cable. Both settings persist in `state/emulator.ini`.
CLI/probe equivalents are `--battery N` and `--charging`.

The per-model `battery_guest` tuple supplies:

| Purpose | XEDK1 address |
| --- | --- |
| Post-initialization idle entry | `01151010` |
| Guest allocator | `011537AE` |
| SendBatteryReport | `011EAE78` |
| DetectCharger | `011EB8DE` |
| GPIO reader | `01145AC8` |
| Charger GPIO index variable | `00406DE9` |

Level changes allocate a native four-byte battery report (millivolts and
temperature), then call SendBatteryReport. The provisional voltage curve is
`3300 + percent * 9` mV, with temperature 25. Firmware bar thresholds are about
3310, 3510, 3650 and 3800 mV, so its four-bar indicator cannot represent every
percentage distinctly. This is not a calibrated cell or discharge model.

Only the configured charger GPIO read is intercepted. It is active-low;
DetectCharger performs its own debounce and invokes the native charger callback.
The firmware, not a host overlay, draws the charging status. A read-only boot
probe with 25% and cable connected reached the native `Charging...` display.

Calls use a synthetic return sentinel (`FFFF0000`) rather than the idle address:
an idle visit from another RTOS task must not be mistaken for a function return.
Battery injections and content imports are serialized by the frontends.

The charger path also writes and polls bit 3 at `0004140C` from `01120450`.
The model supplies a mutable register readback stub there; returning zero forever
would hang the firmware in its hardware-lock loop. Other charger hardware and
analog measurements remain unimplemented.
