# C100 startup after the GPRS splash

Without provisioning, erased storage leaves the guest at the splash.
The frontend now provisions missing NVM using guest factory service routines,
then boots to **Insert SIM Card**; see [the follow-up analysis](c100-nvm.md).
This is not a completed phone boot.
The following fixes are hardware behavior, not firmware patches or guest RAM
injections. ROM files remain unchanged; flash commands modify host-owned copies.

## IRQ critical sections

XEDA2 `ds_MaskInterrupts_asm_805` at `01389674` briefly masks CPSR IRQ/FIQ,
reads the halfword at `00040902`, clears bit 7, writes it back and restores CPSR.
The matching restore routine at `013896AC` restores bit 7 if it was previously
set. XEDK1 equivalents are `0138A37C` and `0138A3B4`.

Ignoring this hardware gate delivered IRQs inside firmware critical sections.
Bring-up profiles now contain latches at `40900` and `40904`, and
`irq_gate=0x40902,0x80`. Closing the gate suppresses CPU IRQ delivery without
discarding pending sources. Reopening it delivers a pending enabled IRQ.
FIQ gating is not inferred from this code. Tests verify the pending/delivery
behavior and reject a gate without its mapped latch/controller.

## Flash boot-sector geometry

All three BIN1 images have a sector-offset table at file offset `16A0`:

| Table entries | Offsets relative to first-bank API base `01000000` |
|---|---|
| 0, 1, 7, 8 | `0`, `2000`, `E000`, `10000` |
| 135 | `1000000` (physical second-bank base `02000000`) |
| 262, 263, 264, 269 | `17F0000`, `17F2000`, `17F4000`, `17FE000` |

Thus bank 0 starts with eight 8 KiB boot sectors, and bank 1 ends with eight
8 KiB boot sectors. Regular sectors are 64 KiB. The previous uniform 64 KiB
erase implementation wrongly erased neighboring small sectors. Geometry is
now model data; regression tests verify that adjacent small sectors survive.
This fix alone does **not** remove the remaining startup failure.

## Current failing path (XEDA2)

The UI receives `mmi_NVM_INIT_CB` at `011A770E`, then
`mmi_NVM_LOADING_CB` at `011A6B66`, events `205`, `207`, `208`.
The first descriptor is `tStack` at RAM `00406654`.
Its lookup via `011640AC` returns 4 (record absent). The firmware's default
setup at `0114068E` queues a write request, primitive `5C6`, via `0116420C`.

Before servicing that request, NVM task code at `01220608` validates its
in-RAM NLT/ENT metadata checksums. Observed stored checksums at `0067705A`
and `0067705C` are zero; the calculated checksums of the current empty buffers
are `FFC4`. The mismatch path reaches `01220750`, calls `011645E4` with R0=0
to request initialization (`5D0`), and the resulting `5D1` indication restarts
UI NVM loading. Repetition causes continuing erase/program traffic while LCD
pixel-write count stays at 81,665. Long exploratory runs can exhaust the heap.

The initial `5D1` indication is produced by the scheduler's error-completion
path, not proof of successful NVM initialization. Do not bypass the checksum
test, force a success status, or suppress the allocator/watchdog to advance UI.

## What the supplied files contain

BIN2 starts at `02000000`; the available XEDA2 file is `4C08C0` bytes long,
ending exclusively at `024C08C0`. XEDD1/XEDK1 BIN2 files are `4C0898` bytes.
Firmware sector-table entries used for NVM begin at offset `1500000` from
the API base, physical `02500000`, and extend through top boot sectors.
Consequently **none of these supplied BIN2 files contains this NVM area**.
The loader currently fills their missing tail with erased `FF` bytes.

The user does not know whether these are update packages or phone backups.
Missing factory/settings records are a plausible contributor, but this does
not prove that a full NVM dump is the only solution: the guest's blank-NVM
initialization path and remaining peripheral behavior still need investigation.
No invented factory records, device identity, calibration or checksums are
inserted to mask this distinction. A genuine full backup from the friend's
phone could help distinguish missing state from remaining emulation bugs;
none is currently available.

Further [NVM investigation](c100-nvm.md) found firmware default constructors
and a factory-initialization path. An isolated diagnostic run reaches all 22
UI default records but still fails sector allocation; it is not an unmodified
boot or a successful persistence test. A phone dump is not a prerequisite
for investigating and correcting this path.

## Validation

All synthetic CPU/machine/peripheral/folder checks pass under MSVC x86 C17
`/W4 /WX /RTC1`. All three real-ROM boot regressions still show the genuine
GPRS image at 10 million instructions (15,682 colored pixels, 11,519 edges,
81,665 guest pixel writes). These are splash regressions, not UI-ready tests.
The diagnostic frame period remains 10,000 interpreter ticks; a 100,000-tick
experiment also reproduced the NVM restart and did not establish a CPU clock.
Flash operation counters are available in CLI output and GUI Diagnostics.
