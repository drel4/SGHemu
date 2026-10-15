# C100 battery and charger simulation

Cable detection uses a hardware GPIO input and interrupt. Voltage/temperature
measurements use the ARM/DSP mailbox protocol. The shipped profile no longer
calls battery firmware functions or intercepts their entry PCs.

The native **Emulation -> Battery** submenu accepts a percentage from 0 to 100
and toggles the charging cable. Both settings persist in `state/emulator.ini`.
CLI/probe equivalents are `--battery N` and `--charging`.

## Charger cable: hardware input, not firmware calls

The current C100 profile declares board wiring:

```
gpio_port=0x0004028e,0x04
charger_input=2,5
```

The cable drives GPIO bank B bit 2 active-low. Connecting/disconnecting latches
interrupt 5, including when that interrupt is masked. Firmware reads the GPIO
data register, debounces the input, runs its charger handler and updates the UI.
No firmware PC, allocator, RAM pin-index variable, synthetic return address or
injected DetectCharger call is needed for the cable. The GUI cable control is
available with this hardware wiring alone.

Evidence from the existing Ghidra XEDK1 export and instruction disassembly:
`uhGPIORead` at `01145AC8` reads the x16 address in a pin descriptor. The table
at `013BB774` gives logical pin 10 data address `0004028E`, bit 2 and IRQ mask
`00000020`. The initialized TA pin variable `00406DE9` contains 10; this was
used for analysis only and is no longer part of the current charger config.
The GPIO descriptor table also exists at `013BAA6C` in XEDA2 and `013BB6D0` in
XEDD1: software tables move, while bank data-register addresses stay the same.
This comparison does not mean those other firmwares boot completely.

`gpio_port` implements observed x16 masked data writes: low byte selects pins,
high byte supplies their new output values. External inputs override the output
latch. This is a minimal data-port model, not a complete GPIO block: direction,
mux and programmable interrupt polarity are not yet modeled. Both cable edges
latch IRQ 5; native firmware normally rearms detection for the opposite edge.
The former constant `identity16` at this address has been removed.

Verification: exhaustive data-mask/value checks, external-input isolation,
masked IRQ latching/acknowledgement and invalid accesses. Read-only XEDK1 probes
ran 400 million steps with cable connected at reset (animated charging), and
another 400 million with normal boot then connect/disconnect/reconnect. All three
runtime edges reached native DetectCharger through IRQ delivery, without host
injected cable calls; the CPU remained live. Original NVM was not modified.

## Battery percentage: ADC mailbox inputs

The current profile declares hardware addresses and physical ADC endpoints:

```
dsp_mailbox=0x00060000,0x00040910
battery_adc=107,138,20
```

The host interpolates battery raw codes 107..138 over 0..100%, and supplies
temperature raw code 20. Firmware performs its normal scan, conversion using
factory calibration, filtering/hysteresis and UI reporting. With the pinned
test NVM, native reports were 4208 mV at 100%, 3532 mV at 25%, and 25 Celsius.
The curve is provisional, not a calibrated discharge model; the four-bar display
cannot represent every percentage. Changes follow native sampling/filtering,
not an immediate host UI update.

See [CX805 ADC mailbox](cx805-adc-mailbox.md) for packet formats, pipeline timing,
unsupported functionality and verification. No guest allocator, idle-function
address, report-function address or synthetic return sentinel is used here.

### Legacy compatibility configs

Older private profiles may still use `battery_report` or `battery_guest`. Their
former XEDK1 addresses were:

| Purpose | XEDK1 address |
| --- | --- |
| Post-initialization idle entry | `01151010` |
| Guest allocator | `011537AE` |
| SendBatteryReport | `011EAE78` |
| DetectCharger | `011EB8DE` |
| GPIO reader | `01145AC8` |
| Charger GPIO index variable | `00406DE9` |

These old hooks allocate and inject a report with a synthetic return sentinel
(`FFFF0000`), using `3300 + percent * 9` mV and temperature 25. They are retained
only for compatibility. `battery_adc` disables that injected-report path, and a
physical charger input disables legacy GPIO interception/cable-call injection.

The charger path also writes and polls bit 3 at `0004140C` from `01120450`.
The model supplies a mutable register readback stub there; returning zero forever
would hang the firmware in its hardware-lock loop. Other charger hardware,
current sensing and a full analog/RF/DSP model remain unimplemented.

## Charger-connected reset animation

The frozen charging animation was a firmware scheduler wait, not a halted CPU
or an LCD presentation problem. `OnWakeup` (`011506C4`) waited at `011506CE` for
the low-power state byte at `0066B264` to change from 5 to 1. Its writer is the
native `HWServiceCAL32K` handler (`0115110E`). Frame IRQ 26 still fired, but the
calibration/wakeup timer at `00040700` was unmapped, so UI timer callbacks stopped.

The C100 model now declares:

```
lp_timer=0x00040700,32768,4,25,4615385
```

The shared peripheral implements a latched 18-bit low-power counter, calibration
IRQ 4 and a modulo-counter wake comparator on IRQ 25, driven by emulated
nanoseconds independently of the ARM divider. Firmware writes control 0 before
reading a coherent snapshot, then control 2 and the comparison count to arm wake.
Interrupts use the existing controller's pending/mask logic; no firmware RAM flag,
PC, charging artwork or timer callback is patched by the host.

This is a minimal firmware-derived model, not complete CX805 documentation.
The fine-phase register at +2 remains zero, and calibration cadence uses the
model's GSM-frame period. Exact sub-tick phase and other low-power controls
remain unimplemented.

Verification: 26 peripheral checks cover latching, deadlines, masked/latched IRQs,
18-bit wrap, acknowledgement, cancellation and invalid accesses. Read-only XEDK1
boots with the cable connected at 25% and 100% show different native animation
frames, increasing LCD writes and charging callbacks. The 25% probe stayed live
for 500 million steps (~46 emulated seconds, 41 charging timer callbacks); 100%
stayed live for 400 million steps (~37 seconds, 33 callbacks), including pending
battery reports after the initial boot. Original NVM was not modified.
