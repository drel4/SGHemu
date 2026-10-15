# C100 virtual SIM

## Result and use

C100XEDA2, C100XEDD1 and C100XEDK1 initialize the virtual SIM and reach
the guest's normal standby screen: wallpaper, **No Service**, Menu and Name.
The guest performs its own card reset, parsing, file reads and UI transition.
No MMI state, guest RAM identity or standby pixels are injected by the host.
XEDK1 now also has a configurable high-level virtual carrier; the other
profiles still have no operator registration. See [cellular support](c100-cellular.md).
With the card absent, XEDK1 returns to **Insert SIM Card**.

The three bring-up profiles use:

```ini
sim=0x00040a80,0x00041300,0x00040520
sim_present=1
sim_card=sim/test-sim.cfg
```

Paths are relative to the model config. `sim_card` selects an optional identity
file; defaults are synthetic and require no physical card:

```ini
imsi=001010000000001
iccid=89001000000000000001
spn=SGHemu
```

IMSI accepts 5–15 decimal digits, ICCID exactly 20, and SPN 1–16 printable
ASCII characters. Invalid, duplicate or unknown settings fail loading without
partially changing the card identity.

GUI: **Emulation > Virtual SIM inserted (reset)** toggles presence and cold
resets, preserving running/paused state. CLI: `--sim present` or `--sim none`.
The GUI switch was checked in both directions using the native menu. Phone
NVM remains autosaved. User-writable SIM EFs are saved automatically alongside
that firmware's NVM, with a `.sim` suffix, and loaded on reset/startup. Each
SIM snapshot is bound to IMSI/ICCID and CRC-validated before applying any data.
A rejected snapshot stops loading and is not overwritten. Protocol reset still
preserves the in-memory SIM files.

## Driver evidence

Ghidra analysis uses the XEDK1 linker listing and `ARM:LE:32:v4t`.
Addresses below are XEDK1 Thumb function entry addresses, not cross-version
HLE traps. The same emulated hardware boots all three versions.

| Routine | Address | Observed role |
|---|---|---|
| PSDoStartUp | `0x01155DDC` | Power/reset startup |
| PSStartSIMResetSequence | `0x01155C2A` | Reset and timeout sequence |
| PSInitChar | `0x011558C6` | ATR bytes |
| PSAcceptSIM | `0x01155E3C` | Accept parsed card |
| PSWaitProcByte | `0x011561E8` | T=0 procedure-byte handling |
| EnableDMAWaitStatus | `0x01155E50` | Data DMA and status handling |
| SimResetTimerAIH / SimDataTimerAIH | `0x01156F84` / `0x01156F52` | Timer callbacks |
| SimIH / SimDMAIH | `0x01156FC0` / `0x01157056` | UART / DMA interrupt handling |
| SICheckVcc | `0x011DFDE0` | Requires 3 V characteristic bit in directory metadata |
| SIGetSIMState | `0x011E307E` | Ready state 4 in guest state byte `0x00785DE2` |

The initial directory characteristic value `0x80` passed ATR but failed
SICheckVcc. `0x90` advertises disabled PIN1 plus the required 3 V capability;
the guest then completes initialization. Earlier UART-only emulation also
failed at data transfer: real guest DMA, rather than a fabricated ready flag,
was needed to complete commands.

## Hardware model

| Region | Registers / behavior | IRQ source |
|---|---|---|
| UART `0x40A80`, 16 bytes | Halfwords: control +0, format +2, status +4, mask +6, TX +8, RX +A, threshold +C | 3 |
| Timer `0x41300`, 16 bytes | Counter +0, load +8, mode +C; suspend bit 3 | 0 |
| DMA `0x40520`, 32 bytes | Control +0, source +4, destination +8, threshold +C, end +10, current source/destination +14/+18 | 10 |

UART control bits 0/1 enable supply/clock and bit 3 releases reset. A rising
reset edge on an inserted powered card produces ATR `3B 00`. Status bits
`0x08` and `0x10` report RX readiness and TX completion, gated by the mask.
The previous cold-start halfword fixture is retained as initial register
state (`0x20017420` as a little-endian word), not as a permanent read override.

DMA enable initializes its current pointers; control direction `0x10` sends
RAM bytes, `0x08` receives into RAM. Reaching the inclusive threshold stops
DMA and raises its interrupt. Transfers use the bus and validate ordinary
memory ranges; device targets and ROM writes are rejected.

Timing is provisional: TX completion 500 interpreter ticks, ATR/RX/DMA
spacing 1,000 ticks, timer counter multiplied by four. These choices model
ordering and timeouts, not a measured clock or electrical SIM interface.

## Card protocol and limits

`src/sim_card.c` implements a bounded T=0 command/response engine behind
the transport in `src/sim.c`. It handles SELECT, GET RESPONSE, STATUS,
READ/UPDATE BINARY, READ/UPDATE RECORD and the observed sleep command.
Files include MF/DF metadata, ICCID, IMSI, administration/phase/language,
PLMN selection, location/access information, service table, SPN, emergency
number, and small empty ADN/SMS/MSISDN/SMSP stores. Metadata and encodings
follow the relevant GSM SIM structures in
[ETSI TS 100 977 V8.14.0 (GSM 11.11)](https://www.etsi.org/deliver/etsi_ts/100900_100999/100977/08.14.00_60/ts_100977v081400p.pdf).

This is a minimal boot card, not full SIM conformance. PIN1 is disabled;
SMS and SMS-parameter storage services are advertised. Authentication, PIN operations and
toolkit commands return unsupported-command status rather than fabricated
authentication results. There is no physical GSM radio or subscriber authentication. The XEDK1 virtual
carrier supplies synthetic registration and local call/SMS completions. Phone
NVM and persistent SIM storage remain separate devices. EF_SMS has ten 176-byte
records; EF_SMSP supplies a synthetic service-centre number. Incoming host SMS
are single-part UCS-2 (up to 70 characters, without surrogate pairs).

## Verification

`build.cmd release` runs the synthetic SIM/card/transport tests, including:
APDU framing, file selection/metadata/encoding, read/write boundaries,
configuration validation, reset, timer/UART IRQ behavior, RX/TX DMA,
threshold completion, invalid DMA targets, SMS-DELIVER encoding, record
exhaustion, storage reload, CRC rejection and identity mismatch rejection.

The optional real-ROM regression takes a completed read-only phone-NVM
snapshot, executes 100 million instructions, checks transport completion,
and saves guest LCD pixels:

```powershell
.\build\c100_sim_test.exe models/c100/c100xedk1-bringup.cfg COMPLETED.snv present build/sim-k1-present.bmp
.\build\c100_sim_test.exe models/c100/c100xedk1-bringup.cfg COMPLETED.snv none build/sim-k1-none.bmp
```

Historical boot-only results before SMS services: inserted A2/DD1/K1 each
completed 41 APDUs and 450 DMA bytes;
their captures show normal standby. Absent K1 completed no APDUs and shows
Insert SIM Card. Captures/logs are ignored `build/sim-*-present.*` and
`build/sim-k1-none.*`. A separate fresh erased-NVM A2 run completed both
automatic provisioning resets and reached the same virtual-SIM standby
(`build/sim-first-boot.*`). ROM inputs and imported NVM snapshots were not
modified by these regression runs.
