# SGH-C100 C100XEDA2: initial memory map

## Inputs and confidence

Analyzed locally on 2026-10-09 with Ghidra 12.1.2 (`ARM:LE:32:v4t`, the actual
language ID is lowercase `v4t`), bootstrap instruction traces and ResMan's
existing `C100XEDA2.rxt`. Ghidra's initial disassembly and literal pool are in
[c100-boot-ghidra.txt](c100-boot-ghidra.txt). This is a bootstrap map, not a
complete CX805 hardware register specification.

| Input | Bytes | SHA-256 |
|---|---:|---|
| `C100XEDA2_1.bin` | 7,166,964 | `328a720453537253928096765ff6b6f18dc560fc66e9c7a6bf01131223062793` |
| `C100XEDA2_2.bin` | 4,982,976 | `811b468a0ef863c435fee89ba524b643266913f3410a4245d7dcaa403c6508c8` |

The first image begins directly with eight little-endian ARM branches. Local
bootstrap instructions and absolute literal addresses agree. No decompression
or decryption is needed to execute this bootstrap. Embedded signature hits are
not proof of a wrapper or whole-image compression. Code/data sections farther
into the image may still contain compressed assets. The second image begins
with zeros and structured data. The subsequently supplied XEDK1 linker map
establishes LOAD2/RDATA at `0x02000000`; both images are now mapped. See
[LCD/boot follow-up](c100-lcd.md). The first image
has `C100XEDA2` at file offset `0x38F122`,
matching the existing ResMan descriptor.

Both files carry their exact little-endian file length at offset `0x40`:
`0x006D5BF4` and `0x004C08C0`, respectively. The low halfword at `0x44` is
`0xCBCB` in both. `fwinspect` recognizes this split-image header candidate.
BIN2 has 64 initial zero bytes and repeating structured records from `0x60`
(first bytes `09 03 01 00 FF FF 00 00`). Resource/RLE content is a hypothesis,
not a proven decoder or mapping. Its lack of reset vectors is not evidence
that it can be ignored. Aligned apparent pointers to `0x02000060`,
`0x020000DC` and `0x03000060` in BIN1's assets turn out to be embedded pixel/
compressed data, not sufficient evidence of a load address.

## Why flash base is 0x01000000

The reset vector word is `EA400092`. Executed at address zero, its ARM branch
target is `0x01000250`. The code at file offset `0x250` is a coherent reset
routine. The separate branch at file offset `0x54` (`EA00007D`) targets that
same offset when loaded at `0x01000000`. Literal `0x010004C4` at file offset
`0x608` points into the bootstrap; literals `0x0138F17E` and `0x016D0234` also
point into the supplied ROM. ResMan independently names base `0x1000000`.

Running the vector itself at flash base would instead jump to `0x02000250`;
that is not the observed local reset routine. The current configuration starts
at zero with the first 32 ROM bytes aliased there. Full boot-window alias size,
remapping and RAM replacement of exception vectors still require RE.

| Exception | Vector word | Target when executed at zero |
|---|---|---|
| Reset | `EA400092` | `0x01000250` |
| Undefined | `EA400061` | `0x01000190` |
| SWI | `EA400064` | `0x010001A0` |
| Prefetch abort | `EA400068` | `0x010001B4` |
| Data abort | `EA40006B` | `0x010001C4` |
| Reserved | `EA40006E` | `0x010001D4` |
| IRQ | `EA4E25B5` | `0x013896F4` |
| FIQ | `EA40006D` | `0x010001D8` |

## Address regions

All ranges below use an **exclusive** end address.

| Address / range | Evidence | Current implementation |
|---|---|---|
| `0x00000000..0x00000020` | Reset/exception branch vectors | Read-only alias of first ROM bytes; provisional alias extent |
| `0x00000020..0x00002FA0` | Bootstrap copies ROM data to `0x20`, then zeros low BSS ending at `0x2FA0` | RAM for the bootstrap-used span; not a physical capacity claim |
| `0x00002FFA` | First `LDRH`, PC `0x01000254`; compared against `0x16`, then `0x11` | Unmapped; purpose unknown (boot/hardware selector hypothesis) |
| `0x00002733` | Byte read after special `0x16` path | Falls in bootstrap RAM span; semantics unverified |
| `0x00040000` area | Many explicit halfword register accesses, detailed below | Unmapped MMIO, every access logged |
| `0x00080AD1`, `0x00081063`, `0x00080F9D` | Conditional reset path indirect code targets | Not present/mapped; possible internal boot ROM, **unconfirmed** |
| `0x00400000..0x007E05C0` | Copy/BSS loops and initial stacks; see below | RAM for bootstrap-used span, not a chip-capacity claim |
| `0x01000000..0x016D5BF4` | Exact first-image size and coherent absolute pointers | ROM, exact bytes; write attempts logged |
| `0x02000000..0x02800000` | LOAD2/RDATA at `0x02000000` in XEDK1 linker map | Second ROM, FF padding; provisional 8 MiB flash window |
| `0x00C00000`, `0x00C80000` | LCD command and pixel ports from guest driver | S6B33-family command/RGB565 interface |
| `0x00040100..0x00040140` | Class-3 interrupt driver | Pending, enable, FIQ routing; diagnostic source bit 26 |
| `0x00040300..0x00040304` | IICStartCondition/ShiftByteOut/In | PMIC serial pin interface, provisional register defaults |
| `0x00040A80..0x00040A90` | PS SIM UART driver | Reset, T=0 byte transport and IRQ source 3 |
| `0x00041300..0x00041310` | SIM timer Set/Suspend/Resume routines | Instruction-time timeout, IRQ source 0; timing uncalibrated |
| `0x00040520..0x00040540` | SIM DMA driver | Byte DMA to/from guest RAM, threshold IRQ source 10 |
| `0x00040800`–`0x00040805` | Active-low keypad matrix | Control, row drive, column sense; guest scanning/debounce |
| `0x0004028C`, `0x0004028E` | GPIO data banks A/B | Masked output writes, physical RGB LED and active-low charger input |
| `0x00040700..0x00040708` | Low-power oscillator/counter | Calibration IRQ 4 and wake comparator IRQ 25; fine phase remains unimplemented |
| `0x00060000..0x00060800`, `0x00040910` | ARM/DSP exchange window and handshake | ADC mux/read subset with pipelined results; not a full DSP |
| Full DSP/RF and remaining analog peripherals | Not yet implemented | Unknown accesses/unsupported mailbox opcodes logged |

### RAM initialization

The SIM mappings are derived from XEDK1 driver decompilation and verified with
all three supported firmware versions; see [SIM register evidence](c100-sim.md).

The Thumb loops beginning at `0x01000418` supply stronger RAM evidence than
guessing a convenient memory allocation:

| Source | Destination | Initialized end | BSS size / end |
|---|---|---|---|
| `0x016D0234` | `0x00400000` | `0x0040052C` | `0x310C`, ends `0x00403638` |
| `0x016D0760` | `0x004062B0` | `0x0040B740` | `0x3D4E80`, ends `0x007E05C0` |
| `0x016D0234` | `0x00001350` | `0x00001350` (empty copy) | `0x1C50`, ends `0x00002FA0` |
| `0x016CEF94` | `0x00000020` | `0x000012C0` (`0x12A0` bytes) | Zero-length additional BSS |

The supervisor SP is initialized to `0x007E05C0`; other literals name
`0x007E0140`, `0x007E01C0`, `0x007E00C0`. Crash/exception state is stored around
`0x0040C4FC..0x0040CC3C`. Physical RAM geometry, holes, wait states and the
boot-time mapping mechanism remain unknown.

## Early MMIO accesses

The names below describe observed operations, **not** proven peripheral roles.
Do not label these registers as clocks/timers/memory controllers yet.

| Address | Width | PC / observed operation | Path |
|---|---:|---|---|
| `0x00002FFA` | 16 | `0x01000254`: read; compare with `0x16`, then `0x11` | Always |
| `0x00041202` | 16 | `0x01000268`: write `0x0066` | Selector == `0x16` |
| `0x0004120A` | 16 | `0x01000270`: write `0x0066` | Selector == `0x16` |
| `0x00041200` | 16 | `0x0100027C`: write `1` | Selector == `0x16` |
| `0x00041208` | 16 | `0x01000284`: write `1` | Selector == `0x16` |
| `0x00040B02` | 16 | `0x01000290`: write `0x0901` | Selector == `0x16` |
| `0x00040284` | 16 | `0x0100029C`: write `0xFEFE`; later `0xFFFF` | Special path |
| `0x00040108` | 16 | `0x010002A8`: write `0x0140`; later zero | Special path |
| `0x0004010C` | 16 | `0x010002B0`: write `0x0140`; later zero | Special path |
| `0x00040340` | 16 | `0x0100037C`: write zero | Normal init path |
| `0x00040010` | 16 | `0x0100038C`: write ROM table halfword `0x0105` | Normal init path |
| `0x00040012` | 16 | `0x01000398`: write table halfword `0x0105` | Normal init path |
| `0x00040014` | 16 | `0x010003A4`: write table halfword `0x0105` | Normal init path |
| `0x00040016` | 16 | `0x010003B0`: write table halfword `0x0105` | Normal init path |
| `0x00040018` | 16 | `0x010003BC`: write table halfword `0x0105` | Normal init path |
| `0x00040020` | 16 | `0x010003F4`: write table halfword `0x0105` | Normal init path |
| `0x00040022` | 16 | `0x01000400`: write table halfword `0x0105` | Normal init path |
| `0x00040024` | 16 | `0x0100040C`: write table halfword `0x0105` | Normal init path |
| `0x00040294` | 16 | `0x01122CEC`: read | Later hardware-validation helper |
| `0x00040298` | 16 | `0x01122CF8`: read | Later hardware-validation helper |
| `0x00040296` | 16 | `0x01122CF2`: read | Later hardware-validation helper |
| `0x00002FFA` | 16 | `0x01122CFE`: read again | Later hardware-validation helper |

The conditional path also clears `0x00041200/08/02/0A`, and clears
`0x00040108/0C`; it calls addresses in the possible internal boot ROM before
returning/jumping into the application. These paths have not been executed
with confirmed hardware inputs.

## Earlier strict/zero-response trace (historical)

Default execution runs three instructions and logs:

```text
2 PC=01000254 R16 00002ffa value=00000000 unmapped
```

Diagnostic zero-response execution with a budget of 3,200,000 instructions
gets through copy/BSS initialization with 14 logged unknown accesses, then
remains at `0x0111F3F0`. This is a self-branch after the validation call from
`0x0111F3E6` returns zero; **not a successful boot**. The firmware calls
`0x0115C81A` before this failure check. Next: RE that validation function and
the selector/register helpers, determine the expected hardware identity, then
implement only the confirmed behavior. Do not force the branch or return a
made-up chip ID just to progress.

Follow-up: [hardware-validation research](c100-validation.md) now records the
classifier and driver selection tables. Its separate experimental profile
passes this failure check for a firmware-accepted identity tuple, but the
actual C100 revision values remain unconfirmed. The strict default model is
unchanged.

At this stage the CPU clock was still unknown. Later [XEDK1 PLL analysis](c100-clock.md)
established firmware-reported mode-2 clocks of ARM 23.4 MHz and DSP 46.8 MHz.
Those physical rates are not yet used as calibrated interpreter timing.
Next timing work must identify divider/timer writes and their source clock,
then compare a measured reload period with the GSM frame. No MA-3 loop-mode
bit has been identified yet.

## Register-bank follow-up

Historical class-3 bring-up findings below describe the original identity.
The current three bring-up profiles select the firmware's 40-pin `80500_31`
driver and map its auxiliary interrupt bank at `00041700`, GPIO E at `0004029C`
and read-only internal-ROM version/memcpy vectors at `00080030`/`0008003C`.
See [charger completion and identity evidence](c100-battery.md).

[C100 I/O disassembly](c100-io-ghidra.txt) is generated by `C100Io.java` from
the class-3 driver selected into RAM. At `0x01217DCC`, the literal is
`0x00040900`. Function `0x01217D00` reads/modifies/writes halfwords at offsets
`+2` and `+6` with `(old | set) & ~clear`; the second result is masked to 11
bits. Getter `0x01217D9A` returns those halfwords. The extended set/get pair
at `0x01217D2E` / `0x01217DA8` also touches `+0x20` and `+0x22`; a literal
`0xEFEF` masks one split-register write. Nearby index-validation helpers
distinguish indices `0..31` and `32..39` with exclusions. A GPIO/pin-control
role is plausible, but directions, input/output status and reset values are
not yet established. General GPIO behavior remains unimplemented; the observed
keypad matrix is now connected as described in [keypad notes](c100-keypad.md).

For reproducible exploration, `--until 0x0115c828` stops before executing the
driver-selection return. With the lab profile and continuation enabled, this
occurs after **3,056,521** instructions with `R0=1`, `R4=3` and 15 faults.
`--mmio-trace` records successful device accesses as well as unknown accesses
in `0x40000..0x42000`; it does not suppress faults or alter read/write values.

## First visible LCD follow-up

The bring-up profiles now reach firmware-generated GPRS LCD output. The strict and identity-only traces above remain historical diagnostic evidence, not the current bring-up limit. See [LCD/PMIC/IRQ evidence and provisional fixtures](c100-lcd.md) and [model folders](firmware-layout.md). Firmware binaries now live under models/c100/roms/. Unknown MMIO remains logged; neither clock frequency nor the actual revision identity is confirmed.
