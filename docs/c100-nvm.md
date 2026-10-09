# C100 persistent storage investigation

**Current behavior:** the GUI/CLI automatically persist NVM and provision a
missing save with the guest's factory formatter plus initialization-marker
writer. The earlier sections retain the investigation history. See the final
section for the current first-boot path and its limitations.

## What “300 KB free for the user” means for emulation

The user reports a 300 KB user-memory specification on the Legacy Portable
Computing wiki. That number has not yet been independently verified or matched
to a firmware quota calculation. It must not be used as the size of the entire
emulated NVM region. Whether KB means 1,000 or 1,024 bytes is also unresolved.

The firmware sector table references storage starting at physical `02500000`
and continuing into the second flash bank's top sectors. The supplied BIN2
files end before that region. The emulator fills the missing region with
erased `FF`, not fabricated phone data. See [post-splash tracing](c100-post-splash.md).

User-visible storage can be a quota within a larger persistent record store.
The startup record names below directly establish that C100 NVM is **not just
downloaded melodies, pictures or games**: settings, SMS, phonebook and other
application state also pass through this layer. This does not establish that
all radio calibration or device identity lives in the same store.

Built-in ROM assets are a separate matter. Finding an MMF or PNG in BIN1 does
not imply it is a file in the writable user-content store.

## UI record descriptor table

XEDA2 BIN1 file offset `006D0B04` (mapped address `016D0B04`) contains the
descriptor table copied to RAM `00406654`. Entries are 44 bytes: a 16-byte name,
a 32-bit size at offset 16 and six function-pointer words at offsets 20–43.
The first function pointer is used for default initialization in the observed
loader path. The other callback roles are not all decoded yet.

| Record names | Count | Size field per record, bytes |
|---|---:|---:|
| `tStack` | 1 | 68 |
| `tSetting` | 1 | 880 |
| `callParam` | 1 | 3,984 |
| `tSMSinfo` | 1 | 8,540 |
| `tPIMS` | 1 | 4,076 |
| `upEnv` | 1 | 12,792 |
| `upEnv0` | 1 | 1,720 |
| `upCache` | 1 | 32,768 |
| `pbEntry` | 1 | 1,370 |
| `pbEntry0` through `pbEntry8` | 9 | 3,360 |
| `javaEnv` | 1 | 372 |
| `javaFile`, `javaFile0`, `javaFile1` | 3 | 3,500 |

There are 22 entries followed by an empty-name terminator. These size fields
are not a measured user-content capacity or complete on-flash layout.
Defaults are implemented in the firmware itself; the absence of a real phone
dump does not prove this store cannot be initialized.

## Storage layers and misleading function names

The XEDK1 linker listing names `iRead_NLT`, `iStore_NLT`, stream-record access
and structured flash read/write routines. These are useful starting points
for the proprietary persistent record layer. NLT/ENT metadata and checksums
are observed at runtime. No FAT volume or standard disk filesystem has been
established.

Do not infer a disk filesystem merely from `InitGSMFileSystem`:
XEDK1 `0115492C` is only 22 bytes and initializes six bytes to
`0, 0, 1, 1, 2, 2`. Its placement in `sysstdio.o` suggests channel/stdio setup,
not formatting a user-storage partition. Likewise, `checkJavaFileSystem`
at `0112416C` calls two helpers and then always returns 1; its name alone
does not establish successful storage validation.

XEDK1 `getMaxRmsFileSizeL` at `01123B78` returns 5,120 bytes. This is a
Java RMS-related per-file limit, **not** confirmation or refutation of a
300 KB total user-memory specification. `JAVAFileRead` also accesses Java
environment records, so Java storage needs its own tracing.

## Blank-storage / factory-path experiment

Normal boot still repeats NVM initialization without advancing beyond the
GPRS image. A separate, ignored scratch probe changed **only the first call's
R0 argument** at XEDA2 `011645E4` from 0 to 1 to investigate the firmware's
factory-initialization path. This is a diagnostic intervention, not a fix or
an unmodified boot result. No such override is present in the emulator.

Observed in that experiment:

- `iStore_NLT(1)` at XEDA2 `0116488C` initializes in-RAM metadata and queues
  storage operations.
- The UI loader advances through all 22 default-record callbacks; completion
  event `206` appears at approximately 100.27 million instructions.
- Storage confirmations still carry nonzero status `2`. Advancing the loader
  therefore does **not** establish that defaults were persisted successfully.
- The run subsequently attempts an unmapped internal-ROM fetch at
  `00081FE0`, through `0111AACC` and `01388A36`. Its service contract has not
  been implemented. The LCD still contains the GPRS splash, with 81,665 pixel
  writes; this is not a home-screen milestone.

This experiment demonstrates a firmware default-creation path worth fixing,
without requiring a dump from the friend's phone. It does not justify forcing
factory mode, suppressing failures or injecting invented calibration/identity.

## Narrowing completion status 2

XEDA2 structured-write code `0121D45C` calls `GetMostSuitableSector`
at `01163170` (`01163224` in XEDK1). A short factory-path probe captured:

| Instructions | Caller return PC | Allocator result |
|---:|---|---|
| 5,630,133 | `0121D502` | `0000FFFF` |
| 6,075,645 | `0121D502` | `0000FFFF` |
| 14,632,026 | `0121D502` | `0000FFFF` |

At `0121D504` the guest compares the result with `FFFF`; on equality,
`0121D50A` returns status 2. Thus these failures mean **no suitable sector was
selected**, not that the emulated flash status register returned the value 2.
The exact public error-enum name remains unknown.

The allocator checks per-sector flags at offset `1A` of the 28-byte RAM
sector descriptors. Candidate selection depends on the low 14 flag bits,
bits 14/15 and existing-record lookup; it has a fallback selector at
`01163102`. Flash copies show guest-created `5555 AAAA 5555 AAAA` headers,
but many RAM descriptors retain `FFFF`. The next investigation is how blank
sectors acquire valid allocation flags and how header scanning updates this
table. The signature alone is not evidence of a usable formatted store.

## Erase-counter failure and successful diagnostic provisioning

Further normal-boot tracing identifies the format failure at XEDA2
`012CEA6A`. `EraseAndSetupASector` (`012CEA94`) uses a request byte at offset
9 to select normal versus factory erase-counter handling:

- Normal handling calls `UpdateSectorEraseCounters` at `011635CA`. It reads
  the previous sector's counter through `GetSectorEraseCount` (`01162FB2`).
  A value of `FFFFFFFF` is treated as failure, not as a counter to wrap to zero.
- Factory handling calls `FactoryInitEraseCounters` at `011636BC`, writing
  zero counters in the newly constructed header.
- After successful counter initialization, the sector flag update succeeds;
  header flags become `FFFC` instead of remaining `FFFF`.

A **second isolated diagnostic** requested both factory sector formatting
(request byte 9 = 1) and UI factory initialization (the first `011645E4` call
with R0 = 1). Both interventions were confined to the ignored probe. Guest
code constructed the headers, NLT/ENT metadata and defaults; no host-authored
record payloads or checksums were supplied. All 22 loader records completed
with observed storage-completion status 0. At 55,565,018 instructions the probe
reached the same missing internal-ROM service. This is diagnostic provisioning,
not evidence that an erased phone follows those requests automatically.

The resulting flash tail was saved, then loaded into a **fresh CPU/RAM reset**.
That cold boot had neither runtime override. It reached loader completion
and `00081FE0` at 13,608,957 instructions, with 277 flash programs and **zero
erases**, demonstrating that the guest can reread the prepared persistent
records. LCD writes increased to 84,878, but the visible image was still GPRS.

The XEDK1 listing now identifies the internal-ROM wrapper as `IromBistResults`
(`0138973E`, XEDA2 `01388A36`). Its caller is `UHGetIromVersion` at
`0111AACC`, which passes three output pointers and formats the first returned
halfword. This is not evidence of an IMEI/serial-number service. The output
contract was subsequently checked with Ghidra: only the first halfword is
consumed, solely for version text. Actual ROM-version/BIST values remain
unknown. The caller-specific version HLE now permits a no-SIM UI boot without
fabricating BIST results; see [internal-ROM analysis](c100-irom.md).

## Explicit persistent NVM snapshots

Bring-up profiles declare `nvram=0x02500000,0x00300000`. This is the observed
flash-tail window, **not** a 3 MB user-files quota. CLI `--nv-load FILE.snv`
imports that window before the first instruction; `--nv-save FILE.snv` saves
it after the run (including when stopped at a breakpoint or missing service).
These low-level APIs remain available for diagnostic import/export. Frontends
now use an automatic hash-keyed store, documented below; GUI manual Save has
been removed in favor of automatic persistence and an optional Import command.

Snapshots contain a 344-byte header followed by the raw configured window:

| Header offset | Encoding |
|---|---|
| 0 | Seven ASCII bytes `SGHNVM1` and one zero byte |
| 8, 12, 16 | Little-endian uint32 base, payload size, ROM count |
| 20 | Reserved zero uint32 |
| 24 | Four 72-byte ROM slots: little-endian base/size, 64 ASCII SHA-256 hex characters; unused slots zero |
| 312 | 32-byte binary SHA-256 of the payload |

Loading checks the full header, source firmware hashes, exact file length and
payload digest **before changing any flash bytes**. The configured window
must fit an attached flash bank entirely outside supplied ROM bytes. Saving
uses a newly created adjacent `.tmp` and atomic replacement; an existing
temporary file is not overwritten. A destination identifying a source ROM,
including a hard-link alias, is rejected. `.snv` files are gitignored.

Example (the diagnostic snapshot is local, private and not distributed):

```powershell
.\build\sghemu.exe models/c100/c100xeda2-bringup.cfg 100000000 --nv-load build/c100-xeda2-factory-diagnostic.snv --lcd-out build/c100-after-version-hle.bmp
```

The shared machine core starts with erased flash unless storage is supplied.
Frontends now perform the verified format-and-marker service sequence when
their persistent save is missing. The earlier probe's forced sector flags and
forced UI-initialization argument are not used by that path.
Snapshot tests cover round-trip preservation, damaged/truncated/oversized
files, different firmware, late load rejection, protecting source ROMs,
temporary-file collisions and invalid storage windows. All 629 synthetic
checks and all three original real-ROM splash regressions pass with MSVC x86
C17 `/W4 /WX /RTC1`.

## Remaining work

1. Decode the service packet transport so the now-identified factory operation
   can eventually be requested through emulated hardware, not direct entry.
2. Decode additional internal-ROM services as actual callers require them;
   the version-text caller is now handled, but full BIST behavior is unknown.
3. Trace the no-SIM UI state, connect guest keypad input and validate navigation.
4. Find the user-content quota calculation before configuring a 300 KB limit.

Scratch traces, RAM/flash copies and ROM-derived assets stay under ignored
`build/`. Production ROM files and normal boot behavior were not modified
for this investigation.

## Ghidra follow-up: factory commands versus normal record initialization

Headless Ghidra decompilation of XEDK1 now traces the two distinct paths.
Addresses below are **XEDK1**, not the XEDA2 addresses in the earlier probes.
The reusable [C100Nvm.java](../tools/ghidra/C100Nvm.java) script identifies
direct Thumb BL callers from the supplied listing, decompiles the selected
functions and emits instruction evidence. Private output is
`build/c100-nvm-decompiled.c`. Raw BL candidates alone are not treated as
proof of an executable path; decompiled use and instructions are cross-checked.

### UI record initialization is not sector factory formatting

`flash_Initialize` (`01164698`) sends message `5D0`. Argument 1 selects
request byte `0E` = 1; argument 0 selects byte `0E` = 2. The storage scheduler
routes those requests to NLT metadata initialization/read paths. This argument
does **not** itself request factory erase counters in every flash sector.

`mmi_FlashInit_CB` (`011A6B2E`) explicitly calls `flash_Initialize(1)`;
`mmi_FlashReload_CB` (`011A6B46`) calls `flash_Initialize(0)`.
`mmi_NVM_INIT_CB` (`011A7F2A`) also chooses the argument when a record `0192`
read completes (UI event `20B`). It requires storage status byte at offset 9
to be zero and compares the returned record's first halfword against `FFFF`.
That chooses the byte `bInitMode` at `006E3D1B`. On completion event `204`,
mode 1 is cleared and a mode-0 initialization is requested. A failed record
read clears the pointer and mode rather than automatically factory-formatting
all sectors.

### Explicit factory/service formatting path found

`process_data_packet` (`011EC694`) has separate command cases:

| Command | Observed firmware action |
|---|---|
| `40` | Call `InitializeFlashAPIData` (`0121CFF6`), then `write_Flash` if formatting succeeded |
| `41` | Queue message `422` with callback `ft_flapi_init_callback`; asynchronous formatting path |
| `48` | Queue the read-only initialization message `424` |
| `49` | Call the separate read-only-sector initializer |

These are internal handler command numbers, **not a complete cable protocol**.
Packet framing, validation and physical transport still need decoding.

`InitializeFlashAPIData` and `ParallelInitFlashAPIData` (`0121D06E`) loop
through the sector count, calling `FactoryEraseAndSetupASector` (`012CF9D2`).
The latter allocates a request and explicitly writes **byte 9 = 1** before
calling `EraseAndSetupASector` (`012CF79C`). That selects
`FactoryInitEraseCounters`, exactly the branch exercised by the earlier
diagnostic probe. `FlashAPIScheduler` (`0121DA7C`) routes message `422`
(`376 + AC`) to the parallel initializer. This is a firmware-supplied factory
formatting operation, not a host-invented sector format.

The startup function `main` calls `check_for_factory_loopback` (`01147AEC`).
It opens device `0D` with a configuration containing `4B00` (19,200), writes
byte `2A` (`*`), and checks for the same byte received back through `get_byte`.
A positive result changes the factory-mode startup flag. The exact transport
mapping and timing are not yet established, so the emulator does not fake a
successful loopback.

**Inference:** a completely erased storage pool may require explicit factory
provisioning rather than ordinary phone startup automatically formatting it.
The code now provides a concrete service path to investigate. This does not
yet prove that there is no automatic blank-storage path elsewhere, or that
the service transport is emulated sufficiently to run this command normally.
At this investigation stage no runtime factory flags, guest branches or ROM
bytes were changed. The implementation described below subsequently made
the decoded service format-and-marker sequence an explicit frontend operation.

### Link from service formatting to boot-time default creation

Further decompilation of `write_Flash` (`01149C8E`) provides a concrete bridge:
it accesses record `0192`, and its argument-0 path writes `FFFF` into the
record's next halfword (argument 1 ORs in `0101` instead). The record identifier
is a separate first halfword in the write request; it is not part of the
returned payload checked by `mmi_NVM_INIT_CB`. The function also prepares a
separate `0190` record. These are observed service writes, not evidence that
the complete calibration/identity store has been reconstructed.

Thus the service code can format sector headers, write the `0192` initialization
marker, and let ordinary UI startup select default creation on a subsequent
boot. `FactoryTestMain` receives commands in a device packet loop and calls
`process_data_packet`; it does not unconditionally format flash on entry.
`validate_data_packet` checks command-specific minimum lengths and optional
validation, further confirming these are explicit service requests.

This supports the user's hypothesis of a **post-flashing provisioning step**.
It does not establish whether every firmware update performs it: the supplied
BIN1/BIN2 alone contain neither the PC flasher's command sequence nor a full
post-flash storage image. A normal update might preserve existing storage;
that possibility is not settled by this code. The next implementation target
is the decoded service transport, rather than silently treating all erased
flash as already initialized.

## Implemented first-boot provisioning and automatic persistence

A marker-only diagnostic executed guest `write_Flash(0)` against erased XEDK1
storage. It returned **0 (failure)**, and the screen remained GPRS at 80 million
instructions. A marker cannot be stored reliably without valid sector metadata.

A second diagnostic executed `InitializeFlashAPIData` and then `write_Flash(0)`:
formatter result 0 (success), writer result 1 (success), 103 erases and 14,384
program operations. Saving immediately and cold-booting that store reached
**Insert SIM Card**, without the earlier forced erase-request byte or forced
`flash_Initialize(1)` call. The marker is read and defaults are created by
ordinary guest startup. A cold reset matters: by the provisioning trigger,
that boot's UI record read had already failed against the originally erased store.

Bring-up configs now declare verified guest entry PCs:

| Firmware | Trigger: flash_Initialize | Formatter | Marker writer |
|---|---|---|---|
| XEDA2 | `011645E4` | `0121C70E` | `01149BFA` |
| XEDD1 | `011646EC` | `0121D0F6` | `01149CE2` |
| XEDK1 | `01164698` | `0121CFF6` | `01149C8E` |

The frontend's first-boot controller waits for initialized guest context at
the trigger. It invokes the real Thumb formatter, then the marker writer with
R0 = 0, using an interworking return to that trigger. The CPU interpreter,
interrupt scheduler and flash hardware execute their instructions normally.
On successful returns, storage is committed atomically and the frontend cold
resets. It does **not** claim to emulate factory cable packets: direct guest
service entry is an explicit high-level provisioning approximation. Original
ROM bytes and on-flash record/checksum construction are not patched or invented.

Persistence is frontend-owned, under ignored `state/<identity-hash>.snv`.
The identity includes the NVM window and original firmware SHA-256 hashes plus
ROM mapping, so renamed files share their state and different versions do not.
GUI checkpoints changed storage every five seconds while running, and on pause,
reset/model change and normal close. CLI saves on completion. Import remains
available, but the GUI has no manual Save menu. Writes use the existing atomic
snapshot replacement; unchanged payloads are not rewritten.

A separate exclusive lock prevents two emulator processes opening the same
state simultaneously. Invalid saves fail closed and are never silently erased
or reformatted. Missing saves alone trigger provisioning, and incomplete or
failed provisioning is not saved. Provisioning has a 100-million-instruction
service limit. Host crashes during ordinary emulation can lose changes since
the last checkpoint; completed provisioning survives a restart.

CLI `--state-dir DIRECTORY` isolates a test store; `--no-persist` disables
both automatic storage and provisioning for original erased-storage diagnostics.
Private first/second-boot captures and logs stay in `build/`. Synthetic NVM
checks now total 138, covering snapshots, round trips, lock exclusion, corrupt
state, failed writes, version isolation, and service provisioning/failed returns.
The total synthetic suite, including keypad checks, is **678 checks**.

### Final reset after default creation

The first reset alone could leave the visible splash while default records were
still being programmed; a manual reset then showed Insert SIM Card. The first
provisioned session now waits for the caller-specific internal-ROM version query,
observed after UI record loading, commits defaults and cold-resets once more.
This is a bounded first-boot handoff, not a repeated or time-based reset. Existing
saves do not enable it. A fresh XEDA2 store (`build/final-reset-result2.txt`)
completed both handoffs, then reached Insert SIM Card at 100 million final-boot
instructions with 124,814 LCD writes and zero flash programs/erases.

Real-ROM validation: fresh independent stores for XEDA2, XEDD1 and XEDK1 all
completed provisioning, cold-reset, and rendered the same **Insert SIM Card**
LCD bitmap at 100 million post-reset instructions (114,433 pixel writes,
102,203 programs, zero erases in that reset run). An XEDA2 second launch loaded
the automatic save with no provisioning, **zero flash programs and zero erases**,
and 124,814 LCD writes at 100 million instructions. Logs/captures:
`build/auto-nvm-first-{a2,dd1,k1}.{txt,bmp}` and
`build/auto-nvm-second-a2.{txt,bmp}`. The existing splash regressions use the
shared machine directly and remain independent of frontend provisioning.
