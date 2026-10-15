# CX805 ARM/DSP ADC mailbox subset

This replaces the C100 battery-report firmware hook with a protocol-level device.
It is **not a DSP CPU, GSM baseband, codec or general interface-sequencer emulator**.
Unsupported packets receive no invented response; counters record them and the
optional bus log reports each unsupported opcode once. Other firmware hooks,
including navigation tones and virtual cellular/WAP integration, remain.

## Evidence

The existing Ghidra XEDK1 export, checked against Thumb disassembly, shows:

- `RHWDoSetADCMux` (`011F2E6C`) appends `0202 4814 MUX`.
- `RHWDoReadIaADC` (`011F2E0E`) appends `09C2 52E4 reserved` for monitored reads.
- `FrameTickIRQHandler` (`0114D5EE`) submits the command-word count at `00060000`,
  then sets bit 0 at `00040910`. Bit 4 is also written during handshake setup.
- Requests begin at `00060002`; each header's low five bits count payload words.
- Response header `000602BC` is a word count. `000602BE` is a **start offset** in
  words from `000602C0`, not a response length. Offset zero is used by this subset.
- `ParseDSPMsgs` (`0114CDFA`) accepts the `C0`-tagged response, skips its address
  word, stores the ADC result and raises the native result-ready flag itself.
- `ADService` (`01147924`) has six scan phases: setup battery mux; request battery
  and set temperature mux; request temperature; consume battery; consume
  temperature; complete the scan. The result must arrive one exchange after its
  request. Immediate results incorrectly feed temperature to the battery phase.

RAM pointers/function PCs above document reverse engineering only. None are
used by the device. It responds to physical MMIO writes regardless of CPU PC.

## Implemented protocol

```
dsp_mailbox=0x00060000,0x00040910
battery_adc=107,138,20
```

The exchange window is 2048 bytes. The command area holds at most 349 words;
payloads are bounds-checked before decoding. A zero header terminates the list.
Control writes publish pending results, process the new request list, and clear
start/busy bits synchronously. Exact busy duration is not known. ADC values are
sampled when their request is processed and held unchanged until publication.

Supported requests:

| Header | Address | Meaning |
| --- | --- | --- |
| `0202` | `4814` | ADC mux write; source is bits 7:5 |
| `09C2` | `52E4` | ADC read; response is `09C2 52E4 result` |

Sources 0, 5 and 7 supply battery, thermistor and zero-offset inputs. Other
sources/addresses/commands are explicitly unsupported. The reserved read payload
is not interpreted as an address or size. This subset does not execute downloaded
DSP programs, emulate arbitrary IA commands, or synthesize GSM/RF success.

The provisional battery range is a board input model, not something recovered
from a firmware function address. Firmware factory calibration still determines
millivolts and temperature. In the pinned NVM, the voltage calibration is slope
13600 / offset -520; code 138 gives 4208 mV, code 115 gives 3532 mV. The thermistor
conversion makes raw 20 approximately 25 Celsius with that calibration.

## Verification

Unit checks cover delayed results, battery/temperature ordering, immutable queued
samples, header offset/count distinction, byte/halfword access, malformed and
unsupported packets, handshake completion and rollback after an attach failure.
Native read-only firmware probes disable injected battery reports entirely:
firmware measurement callbacks and SendBatteryReport execute on their own.
NVM input snapshots are never saved over. A 1.5-billion-step normal boot/run
(~137 emulated seconds) survived 100% -> 25% -> 100% input changes with zero
malformed packets; native reports changed to 3532 mV at 25% and subsequently
followed the firmware's slow upward filtering after the return to 100%.
Charging boots at 25% and 100% stayed animated for 400 million steps each.
The actual Yamaha-DLL startup capture remained byte-identical to the preceding
reference WAV (1,140,000 stereo frames; SHA-256
`bfc6626ce4309e5805107ad856b56398beeef2f528cc013f799a56e49d0250c0`).
A real firmware WAP download of Bumer-Zapravka reached Play -> preview -> Save
with the Yamaha DLL active, 1,841,906 nonzero stereo samples and no audio failure
or CPU halt. The service-LED data-port integration has eight-color, unrelated-pin,
same-value timestamp and legacy-observer-isolation checks.
This does not yet establish full alternate-firmware boot.
