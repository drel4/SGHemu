# C100 user-content import

## Current importer (2026-10-13)

Implemented for **C100 XEDK1** through model-declared guest entry points. Use the
filesystem manager's **Import content...** button, or CLI `--import FILE`.
Storage is the ordinary automatically saved firmware-specific NVM snapshot;
the firmware, not the host, constructs its filesystem records.

* Sounds retain the existing type-6 guest writer path.
* PNG/JPEG/GIF/BMP pictures are decoded with Windows WIC, fitted proportionally
  inside 128 x 128 and converted to RGB565. Animated inputs use the first frame.
  The native container has a nine-byte animation header (width, height,
  animation flag, frame count, reserved byte, little-endian bitmap-body length),
  followed by a two-byte format/palette header and little-endian RGB565 pixels.
  The guest writer stores it as type 7. A 32 x 24 checker image was
  accepted as a 1,545-byte picture in a scratch snapshot. On a fresh boot,
  Phone settings -> My settings -> Wallpaper -> Up -> OK displays the imported
  red/green checker through the native image renderer.
* Java packages use **CLDC 1.0 / MIDP 1.0**, as declared by their manifests.
  ZIP stored/DEFLATE manifests are validated and CRC-checked, continuation lines
  unfolded, suite identity and the declared MIDlet class checked, then a JAD
  generated with the actual JAR length. Selecting a standalone JAD is rejected;
  select its JAR instead. Preparation does not guarantee API compatibility.

### Native Java installation, not raw-JAR storage

`java_import` declares the idle PC, startup globals, `SetInitJar`,
`GSMSendUIMessage`, startup message and minimum boot instruction count:

```
java_import=0x01151010,0x0055a6f0,0x01126ba2,0x01146ad8,0x610,300000000
```

After the handset initializes, the importer calls `GSMMalloc` three times,
providing **separate** JAR, JAD and URL allocations. It stages the initial
descriptor pointers in the native startup globals, calls `SetInitJar` and posts
the firmware's Java startup message. Jbed parses, compiles and installs the
suite itself. No host-authored suite registry or fake game menu is used.

The separate allocations matter: `DeleteInitJad` frees the URL and JAD
independently. The initial prototype placed both inside the JAR allocation,
which triggered `SystemErrorHandler(0x19)` through the guest heap checker.
That prototype bug was fixed; it was not bypassed by suppressing the error.

The host reports **installer started**, not installation completed. Wait for
the native **Installed** confirmation before closing the dialog or resetting.
The filesystem manager's **Java games** button reloads saved NVM and opens the
native installed-MIDlet menu; CLI equivalent: `--java-menu`. The ordinary
handset keypad route is WAP & Games -> Downloads -> Games and more (7.3.1).
Bare `--import GAME.jar` allows 1.8 billion instructions; `--java-menu` allows
600 million. These are diagnostic execution budgets, not measured CPU clocks.

### Larger packages and native working-buffer overflow (2026-10-14)

The supplied Stalker2D 1.1.0 (Qplaze, 100,841-byte JAR) and MobileAgent 2.1.8
(@mail.ru, 120,638-byte JAR) both pass CLDC 1.0 / MIDP 1.0 package preparation.
The native installer can nevertheless show **Installed** without adding a suite.
This was reproduced on scratch copies of the current saved NVM, including a cold
reboot and inspection of the native menu. Package filename/model labels were not
used to reject either file.

Tracing XEDK1 `writeL` (`011246A8`) found Stalker2D compilation filling the
compiled-object working buffer: a subsequent 4,096-byte write returns `FFFFFFFF`.
`getBufSize` (`01123C78`) declares 102,400 / 81,920 / 122,880 bytes for handles
0 / 1 / 2, and 5,120 bytes for the other handles. The 100,841-byte ZIP size is
therefore not the size of its compiled representation. Increasing the execution
budget alone does not fix this failure.

An **optional**, default-off compatibility extension is available from Emulation
or CLI `--extended-java`. Model configuration `java_buffers` declares the verified
get/set helpers, native table, reserved emulator-only address and per-buffer size:

```
java_buffers=0x01123c78,0x01123cf8,0x00540f98,0x00800000,0x100000
```

It intercepts only `getBufSize` / `setFileBuf` for handles 0..2, giving each a
separate zeroed 1 MiB buffer. Small-file handles, the native compiler, file writer,
suite registry and VM are untouched. Extra memory is mapped lazily and never
written into the user's ROM files. This is not a hardware-accuracy claim. Enable
it **before reimporting**; keep it enabled when running the larger installed apps.

Release-build verification (private scratch NVM only):

* Stalker2D reaches Installed, then Saved on closing; a cold boot lists it
  alongside Bowling and MIDP Man. Launch currently reports a native
  `java.io.IOException` while preparing the MIDlet; gameplay is not verified.
* MobileAgent installs through the production CLI `--extended-java --import`
  path and persists in the native menu after reboot. Launch reports native
  `java.lang.ClassFormatError: class format error in Midlet`. Its 35 class files
  have version 47.0, while Stalker2D's 27 classes and Bobby Carrot's four have
  version 45.3. The supplied archives are never rewritten to hide these errors.

These tests fix/verify missing installed entries, not universal MIDP execution.

Java injected calls also now return to a private `FFFF0000` sentinel, rather than
the RTOS idle entry. An unrelated task visiting idle cannot advance the import
state machine. The filesystem status now retains the installation instructions
instead of immediately replacing them with the ordinary directory summary.

### Real package regression

The user-supplied `bobby_carrot.jar` is 63,614 bytes and declares Bobby Carrot
1.0, vendor `mob.ua`, MIDlet class `Bobby`, CLDC 1.0 and MIDP 1.0.

Verified using optimized x86 Release builds and scratch snapshots:

1. The production `--import` path reaches the native **Installed** screen.
2. The guest creates its own `FFS_JAVA_07`, `FFS_JAVA_08` and `FFS_JAVA_09`
   records, alongside changes to its Java metadata.
3. A fresh boot from installed NVM lists **Bowling**, **MIDP Man** and
   **Bobby Carrot** in the native MIDlet menu.
4. Selecting Bobby Carrot and launching it currently produces
   `Starting Bobby Carrot failed: java.lang.NullPointerException` in the native
   error dialog. **Installation/persistence is verified; gameplay is not.**
   The root cause of this launch error is not established. The package also
   references optional `javax.microedition.media` APIs; the manifest alone is
   not evidence that this exact build works on the original C100.

The ordinary keypad route (7.3.1), without the emulator's Java-menu launcher,
also lists the persisted game and produces the same launch exception.

`jarinspect GAME.jar` performs package preparation without booting.
`phoneprobe MODEL.cfg SNAPSHOT.snv PREFIX [--java-menu] [--settle N] [key IDs]`
boots and captures native LCD output without saving the input snapshot.
Soft-left is key 0, Up 1, soft-right 2, OK 4, Down 7 and digits 1..9 are keys
9..17. For the three-entry Java menu, Down twice then soft-left launches Bobby
Carrot. All game files, ROMs, captures and snapshots used here remain private
ignored artifacts.

The remaining sections retain the earlier transport investigation; historical
"not yet driven" statements below are superseded by the importer above.

Investigation record for importing MMF melodies, pictures and Java games through
the phone's own transport instead of patching NVM from the host. This continues
the "Import path still being recovered" section of
[the filesystem investigation](c100-filesystem.md) and remaining-work item 1 of
[the NVM investigation](c100-nvm.md).

Evidence is XEDK1 (`C100XEDK1_1.bin`, decompiled in the ignored
`build.old/ghidra/C100XEDK1.c`). Instruction bytes were checked in the ROM
directly; a decompiler rendering alone is not treated as proof.

## Two separate content paths

The firmware has two unrelated ways content reaches storage. They must not be
conflated.

| Path | Entry | Transport | Content |
|---|---|---|---|
| WAP/over-the-air download | `responseProcessUnknown` `013284BC` -> `DevHandleContent` `01159BD0` | simulated WAP response | GIF, MMF, JAD |
| Factory/service cable | `FactoryTestMain` `01147C72` -> `process_data_packet` `011EC694` | serial device channel 13 (`0xD`) | calibration, flash format, indexed records, flat ROM-MI content - **not the media catalog** |

`DevHandleContent` is the natural user-content installer: it recognises
`vnd.sun.j2me.app-descriptor` (Java), a GIF header, and the MMF/SMAF magic
`MMMD` (sizes the file from the big-endian length at offset 4), then allocates
through `flash_NextAvailableUID` / `flash_CheckAvailableRecord` /
`flash_MemoryInfo`. It is reached from the WAP engine, not the cable.

The factory cable path is the one requested for this work, but it turns out not
to reach the installer: no factory command creates a media entry (see "The `58`
range is not where the menus read media"). `DevHandleContent` remains the
natural installer for over-the-air content.

## Transport stack (cable)

```
FactoryTestMain (01147C72)
  loop: SDS_data_available (011ED1CA) poll
        receive_next_packet (011ED7A0)      <- host -> phone
        process_data_packet  (011EC694)     <- validate + dispatch (~0x78 cmds)
        send_packet          (011ED66C)     <- phone -> host
  get_byte (011ED16E) / DevRead / DevWrite   (device API, 010008xx / 0138A4Cx)
  SDSRead (0115FE40) / SDSWrite (0116038C) / SDSOpen (0115F3A0)
  SDSServiceSerialTxBuffer (01160DA6) / SDSServiceSerialRxBuffer (01160E38)
  serial UART registers
```

`check_for_factory_loopback` (`01147AEC`, called from `main`) opens device `13`,
configures `4B00` (19,200) 8-N-1, writes byte `2A` (`*`) and requires the same
byte back through `get_byte`. Only a positive result sets the factory-mode flag
that reaches `FactoryTestMain`. Device 13 is a **USART at `0x40200`** whose bytes
move through the general **DMA controller at `0x40420`** (both below), so the
loopback is not a plain register echo. With the service cable attached the guest
now returns a positive result and `FactoryTestMain` is reached (below).

## Frame format (verified in ROM bytes)

Both directions share a header but differ in one byte.

Host -> phone (**command**), `receive_next_packet` `011ED7A0`:

| Byte | Value | Notes |
|---|---|---|
| 0 | `02` | start |
| 1 | `0F` | direction marker (checked with `0f 28`) |
| 2 | command | |
| 3 | length (`len`) | 8-bit |
| 3..4 | `len_lo`,`len_hi` | 16-bit length **only** for commands `53..58`, `5D`, `60`, `61` |
| ... | `len-1` data bytes | includes a trailing byte that carries the handler status on responses |
| last | checksum | see below |

Phone -> host (**response**), `send_packet` `011ED66C`: byte 1 is `1F`
(`1f 21 = movs r1,#0x1f`), everything else is the same.

`len` is always *payload + 2* (`send_packet` stores `param_3 + 2`). The command
handler receives a buffer of `len-1` bytes and a length of `len-1`.

Checksum: initialise with `0x02 + 0x0F` (`0x11`) for commands, or
`0x02 + 0x1F` (`0x21`) for responses, add every byte from the command through
the data, then append `checksum = ~sum`. The receiver adds the checksum byte and
requires the running sum to equal `0xFF`. Because the two directions use
different bases, a host that speaks the command framing must not reuse the
response offsets.

Handlers reply to bad frames with `send_packet(cmd, byte, 1, code)`:
`1` bad start, `2` bad second byte, `3` bad length, `4` bad checksum.

## Command dispatch and the content command

`validate_data_packet` (`011EC658`) indexes a `0x79`-entry descriptor table at
`_DAT_011ECA14` giving a per-command minimum length and an optional validator.
`process_data_packet` then switches on the command. Relevant cases:

| Cmd | Handler | Purpose |
|---|---|---|
| `02` | `store_calibration_data` `01147E50` | write calibration |
| `03` | `download_calibration_data` `0114836E` | read calibration |
| `40` | `InitializeFlashAPIData` `0121CFF6` + `write_Flash` | factory flash format |
| `41`/`48`/`49` | async flash init/read | |
| `53..56` | `ft_read_large_record`/`ft_write_large_record`/... | large records |
| `57` | `ft_read_downloadables` `01148D96` | read user content |
| `58` | `ft_write_downloadables` `01148DF8` | **write user content** |

`ft_write_downloadables(buf, len, out, outlen)`:

```
offset = *(u32*)buf          // byte offset in the "ROM MI" content region
bytes  = *(u16*)(buf + 4)    // byte length
SyncWriteROMMIData(offset, bytes >> 1, buf + 6)
```

`ft_read_downloadables` mirrors it with `SyncReadROMMIData` (`01161BB6`), whose
bound is checked against `*_DAT_01161C14 * 2` with a base of `*_DAT_01161C18`.
So the command reaches a flat byte range of a "ROM MI" content device.

### The `58` range is not where the menus read media (resolved)

The factory path cannot install user media. Commands `53..58` reach only two
stores, and neither is the one the handset's media menus enumerate:

* `53..56` write logical flash records by index (`ft_write_large_record`
  `01148D18` -> `SyncWriteMMIsLargeFlashRecord` `0121F63E`, small variant
  `0121F59A`); these are the model/settings records.
* `57`/`58` write the flat "ROM MI" region above.

The handset's downloadable-media **catalog is the ordinary NLT flash
filesystem** - the 28-byte records walked by `flash_SearchAll` (`011643B2`) and
`flash_SearchByType` (`01164498`). There is no separate media catalog. The slot
byte at **`+0x0E` is the media type**:

| Type | Class | Created by |
|---|---|---|
| `5` | `UPEnv` (user-profile record) | `UPEnvCreate` `01142A12` |
| `6` | **downloaded sound / melody** | `mmi_Up_Ringtone_CB` `01193292` |
| `7` | picture / image | `EasyImageWriteData` `0117D658` |
| `8` | EMS object store (`objfile`) | `File_CreateObj` `01232A6E` |
| `10` | Java MIDlet | `JAVAFileCreate` `011439CC` |

The ringtone list is built only from type `6`:

* `mmi_DownSoundNum` (`01190A28`) counts type-6 files with `flash_SearchByType(6)`.
* `mmi_Sound_MenuList` (`01190B6E`) and `EasyFile_Ringtone` (`01192924`) add one
  menu row per type-6 file; `EasyFile_Read` (`0119108E`) resolves each row's name
  through `flash_SearchByName` (`01164160`).

The only code that creates such an entry is `flash_aWriteByName`
(`011642C0`, name, `type`, data, length): it builds the record and queues
`iflash_QueueingJob(6)`; return `6` means *accepted, not committed*. Its callers
are handset application code and the WAP installer - **not one factory command**:

* `mmi_Up_Ringtone_CB` `01193292` - the ringtone save path,
  `flash_aWriteByName(name, 6, data, len, ...)`.
* `mmi_EasyFile_Renaming_CB` `01192F1A` - rename write (types `6`/`7`).
* `EasyImageWriteData` `0117D658` (type `7`), `JAVAFileCreate` `011439CC`
  (type `10`), `UPEnvCreate`/`UPCacheCreate`/`PBEntryCreate` (types `5`/`9`),
  `File_CreateObj` `01232A6E` (type `8`).

`ft_write_small_record`/`ft_write_large_record` do **not** call it: they go
through `SyncWriteMMIs*FlashRecord`. `File_CreateObj`/`objfile` is the SMS/EMS
object store (`Rec_*`), unrelated to the ringtone list.

**Resolution (implemented).** The `58` command cannot create a media entry, so
the import does not go through the cable. The emulator makes the guest itself run
`flash_aWriteByName(name, 6, ...)` at a deterministic point in an ordinary boot
and supplies only the name and the file bytes.

## Direct guest-entry import (implemented)

`src/content.c` (`ContentImport`) drives the guest, once per instruction, the
same way `src/nvram.c` drives provisioning: a trigger PC declared by the model
config launches a short state machine of real guest calls. The host writes no
record bytes and never touches the filesystem directly.

Model config key, all four addresses verified XEDK1 Thumb entries:

```
import=<trigger>,<alloc>,<search>,<write>
```

| Role | XEDK1 | Entry |
|---|---|---|
| trigger | `0x0111AAD8` | `UHGetIromVersion` return PC; the RAM NLT table is loaded by then |
| alloc | `0x011537AE` | `GSMMalloc` |
| search | `0x01164160` | `flash_SearchByName`; returns `0` taken, `4` free |
| write | `0x011642C0` | `flash_aWriteByName`; returns `6` = job queued |

Sequence, all in guest code:

1. `GSMMalloc(size + 0x80)` for the file copy, the staged name, the search-result
   scratch and the `out` ticket.
2. Copy the file bytes and the sanitized name into that block.
3. `flash_SearchByName(name, scratch)`; while it reports `0` (taken), append a
   numeric suffix (`ring`, `ring1`, `ring2`, ...).
4. `flash_aWriteByName(name, 6, data, size, out, 0, 0)`. Type byte `6` selects
   the downloaded-sound class the ringtone menu enumerates.

Stack arguments for the writer go on the guest's own task stack, not inside the
heap block. The writer's frame and its callees grow down past the staged name and
would otherwise clobber it before `flash_aWriteByName` copies the name into its
job record - observed as a present-but-nameless entry.

The trigger must be a post-initialization PC. `flash_Initialize` (`0x01164698`,
the `nv_provision` trigger) is too early: the RAM NLT table is built by the flash
task after that (at ~7.2M instructions the FS is still empty; `0x0111AAD8` is
reached at ~11.6M on a persisted boot).

CLI: `--import FILE` needs persistent storage (`--state-dir`, no `--no-persist`);
it defaults the budget to 20,000,000 instructions when none is given.

### Verified round trip (XEDK1, `c100xedk1-bringup.cfg`)

From a freshly provisioned 23-file store in a scratch `--state-dir`:

* 8-byte `ring.mmf` -> guest accepted; `--fs-list` shows 24 files and a type-6
  record `ring` id `0x18`, size 8.
* A second import of the same name creates `ring1` (id `0x19`); it never
  overwrites.
* A 300-byte `tune` import -> `tune` id `0x1A`, size 300, and the exact 300 bytes
  appear contiguously in the persisted snapshot (nvram `0x4588E`).
* After a reboot `--fs-list` still lists every entry, so the flash task persisted
  both the catalog record and the data.

The NLT field at `+0x10` receives a monotonic allocation serial (23, 24, 25 for
the three imports above). Readers never see it: `flash_SearchByName` and
`flash_SearchByType` copy only name, id, type and the size at `+0x14` into their
result record.

## Serial hardware contract (two ports + one DMA controller)

The firmware has **two** serial ports and one shared **general DMA controller**.
Both ports are byte-engines of the same shape; the DMA controller is what moves
their bytes. The old note here conflated the two - device 13 (factory) opens the
USART at `0x40200`, while the SDS/RF port is `0x41200`/`0x40740`.

**Factory/service port, USART `0x40200`** (`USARTOpen` `0115BFB8`), the port
device 13 opens and `check_for_factory_loopback` drives:

| Offset | Address | Use |
|---|---|---|
| `+0x00` | `0x40200` | control: bit0 enable, bit1 arm. `USARTOpen` zeroes it while disabled, then sets bit0 |
| `+0x02` | `0x40202` | format (`0xCB` observed) |
| `+0x04` | `0x40204` | **TX data** (loaded by the TX DMA) |
| `+0x06` | `0x40206` | mode/status |
| `+0x08` | `0x40208` | control B (same bit operations as `+0`) |
| `+0x0A` | `0x4020A` | format B (`0xCB` observed) |
| `+0x0C` | `0x4020C` | **RX data** (read by the RX DMA) |
| `+0x20..` | `0x40220..` | divisor table |

**SDS port, `0x41200` / `0x40740`** - the port `SDSServiceSerialTxBuffer` /
`RxBuffer` and the RF test use. Block A `0x41200`: `+0` control (bit0 enable,
bit1 arm, bits3-5 protocol, bit6 mode; `SDSInitialize` clears bit0,
`SDSDisableAutobaud` sets it, `DMARead` sets bit1 before `DevEnable(3,..)`), `+2`
format, `+4` TX data, `+6` mode/status (bit0 selects the XON path in `DMARead`),
`+8` control B, `+0xA` format B, `+0xC` RX data, divisor table `0011 0022 0044
0066 00CB 0196 032D 0659`. Block B `0x40740` is modem/autobaud (divisor table
`0018 0032 0054 0098 0130 0261 04C2 07FF`; `+0x20` `0x80`; `+0x22` autobaud
status; `+0x26` AT-detect). It is deliberately left unmapped here:
`check_for_rftest_loopback` (`01151BDC`) opens device `10` at `E100` (57,600) on
this port, and serving its echo would divert the boot into `RFTestInit`
(`01151C5C`). Also still unmapped: `0x40320..0x4032C`.

**General DMA controller, `0x40420`** - eight channels at `0x40420 + n*0x40`
(`SetDMAController` `0115DA10`):

| Offset | Use |
|---|---|
| `+0x00` u16 | control: bit0 enable, bit2 interrupt enable, bits3-4 direction (`0x08` device->memory, `0x10` memory->device) |
| `+0x02` u16 | status (`DMABufferStatus`, low 6 bits) |
| `+0x04` u32 | source start |
| `+0x08` u32 | destination start |
| `+0x0C` u32 | threshold (`SetDMAThreshold` writes it; `SetDMAController` clears it) |
| `+0x10` u32 | end of the memory walk (wrap point) |
| `+0x14` u32 | current source pointer (`CurrentDMAPtr` `0115DAF6` dir 0) |
| `+0x18` u32 | current destination pointer (`CurrentDMAPtr` dir 1) |

`USARTOpen` arms two channels: RX (dir 0) `src = port+0xC`, `dst = RAM ring`,
`end = ring + 0x800 - 1`; TX (dir 1) `src = RAM ring`, `dst = port+4`, same span.
The SIM device already owns channel 4 (`0x40520`), so the controller registers
only the channels nothing else claims.

## Byte path (why a byte register alone is not enough)

`USARTRead` (`0115C1CE`) / `USARTWrite` (`0115C2F4`) are **software ring copies**
(`memcpy` through `thunk_EXT_FUN_000000d0`) between the caller's buffer and a
per-port RAM ring. They never touch the port registers: `USARTRead` derives the
count from `CurrentDMAPtr(rx_ctrl, 1)` (the hardware's current RX write pointer,
`+0x18`) and copies from its own read pointer; `USARTWrite` uses
`CurrentDMAPtr(tx_ctrl, 0)` (`+0x14`) for free space. The actual byte move is the
DMA controller:

```
DevWrite('*')  -> USARTWrite -> TX ring (0x402D2C..)
               -> TX DMA channel (dst 0x40204) -> 0x40204
               -> [service cable loops it back] -> 0x4020C
               -> RX DMA channel (src 0x4020C) -> RX ring (0x40252C..)
get_byte -> DevRead -> USARTRead -> copies from the RX ring
```

Two details a naive model gets wrong, both found by tracing:

- `USARTOpen` zeroes `0x40204` during its reset **while the port is still
  disabled**. A model that echoes every write to the data register turns that
  into a spurious `0x00` that lands in the RX ring one byte ahead of `*`. The
  echo must be gated on the control enable bit.
- A free-running memory->device channel fires before the driver has queued the
  byte. The model paces each channel (a start delay after enable, then one byte
  per configured interval) so it cannot outrun the driver's ring producer.

`get_byte` (`011ED16E`) polls `DevRead` with a `0x5A`-iteration timeout, but a
read that returns any value counts as a byte, so `*` must already be in the ring
by the first poll; the pacing places the first DMA transfer between the driver's
`USARTWrite` and that poll.

## Interrupt source word

`SDSServiceSerialTxBuffer` (`01160DA6`) / `SDSServiceSerialRxBuffer` (`01160E38`)
run from the firmware ISR, dispatched on INTC bits 14 (TX) and 16 (RX); they are
the *only* code that touches the SDS port `0x41200`. The factory USART path does
not use them (it is polled/DMA only). `DevEnable`/`DevDisable(3,..)` gate the
lines through the INTC `enabled` mask; the observed boot never enables 14/16, so
this path is unused here.

## Service-mode gating

The port and controller are present in the model but only *brought up* by an
explicit service session (`--service`; a GUI toggle will use the same switch).
Without it the model forces the cable detached and leaves the DMA controller
unmapped:

- cable detached -> no byte loops back, so device 13 still fails and the boot is
  unchanged;
- controller unmapped -> its register read-back (the driver's read-modify-write
  on the control word) does not perturb the RF-test/boot DMA programming.

Measured on `c100xedk1-bringup.cfg` at 20,000,000 instructions: the ordinary boot
ends at `PC=011539CC`, identical to a build with no serial devices at all; with
`--service` the guest reaches `FactoryTestMain` (`01147C72`) at 4,390,332
instructions.

## Implementation plan

1. **Serial device.** `src/sds_uart.c` - one register block, the loopback cable
   gated on the control enable bit, INTC bits 14/16 for the SDS port. Attached at
   `0x40200`; the SDS/RF port stays unmapped.
2. **DMA controller.** `src/sds_dma.c` - eight channels, direction from the
   control word, one byte per channel per step, paced so it cannot outrun the
   driver. Channel 4 is left to the SIM device.
3. **Loopback.** Done: `check_for_factory_loopback` returns positive and
   `FactoryTestMain` is reached, with device `10` still failing.
4. **Host protocol driver.** Frame encode/decode exactly as above, with a
   regression test for the checksum and the 16-bit-length commands.
5. **Import action (done).** The `58` content command reaches only the flat
   ROM-MI device and the indexed records, so it cannot create a media entry. The
   emulator instead makes the guest run its own `flash_aWriteByName(name, 6, ...)`
   at the post-initialization PC `0x0111AAD8` (see "Direct guest-entry import").
   CLI `--import FILE` is verified end to end. Pictures (type `7`) and Java
   (type `10`) reuse the same call path but each has extra per-class records
   (`EasyImageWriteData` `0117D658`, `JAVAFileCreate` `011439CC`) that are not yet
   driven.
6. **Catalog (resolved).** There is no separate media catalog: the handset lists
   downloaded content by scanning the NLT flash filesystem for the type byte at
   slot `+0x0E` (type `6` = sound, `7` = picture). `File_CreateObj`/`objfile` is
   the unrelated SMS/EMS object store.

No ROM byte, host-authored record payload or invented checksum is used at any
step; the guest builds all storage bytes.

## Odd-length downloadable media (2026-10-14)

The host importer previously passed the exact byte length to the guest writer.
`ReadRecordData` (`011611F0`) computes the flash read count as `byte_length >> 1`;
`FlashRead` (`01000C38`) transfers that many halfwords. Consequently an odd-length
file is written correctly but its last byte is not returned to the media parser.

Reproduced with the user's Bumer-Zapravka MMF (19,085 bytes): flash retained its
last CRC byte `B6`, but native readback returned stale `42`. Selecting it after
O-Zone produced zero nonzero PCM samples for all 20 measured seconds. Padding
the stored file to 19,086 bytes with one trailing zero restores the CRC byte on
readback; the same playback route is audible in the first measured second and
throughout the 20-second capture. The MMMD declared length and every original
SMAF byte remain unchanged. The original O-Zone sample is 15,896 bytes and did
not need padding. Test files, NVM and WAV captures remain ignored/private.

The importer now rounds sound and picture storage lengths up to an even byte
count. Re-import previously broken files: existing NVM entries are not silently
rewritten. New imports use a collision suffix rather than overwrite old entries.

## Imported-picture one-pixel shift (2026-10-14)

The original encoder omitted the bitmap body's two-byte format/palette prefix.
The firmware renderer consumes that prefix, so it skipped the first RGB565 pixel
and carried the next row's first pixel into the right edge. The corrected body
starts with `00 00` (raw bitmap, no palette) before the RGB565 pixels; the
animation header's frame length includes these two bytes. The outer animation
header remains nine bytes. No global LCD coordinates or native assets are moved.

A 128 x 64 RGB565 edge-marker test through the native wallpaper selector proved
the difference: old import row 50 had green at x=0 and red at x=127; corrected
import has red at x=0, green at x=1, and blue at x=127, matching the source.
The firmware also requires an even filesystem storage length, as above.
Re-import existing pictures to replace the old representation.

For this saved NVM, the wallpaper selector initially highlighted the previously
saved `brian` entry. Up selected `No wallpaper`, not the last imported picture.
The correct regression route was Menu -> Phone settings -> My settings ->
Wallpaper -> Down twice -> OK, selecting `border128`. Probe routes must inspect
the current selection rather than assume a fixed initial index.

## Bulk import window

**File -> Phone filesystem...** uses two panes: an import list on the left and
the phone's native records on the right. Add multiple files or drag/drop them,
then choose **Import queue >>**. Queued imports run sequentially, with a fresh
boot for each item and an automatic NVM save after the native record commits.
**Stop** leaves the current write running and keeps pending files queued.
Removal and a new import are disabled while a native write is committing.
The right pane normally shows user media and Java records; **Show system
records** exposes the complete low-level directory. Names are firmware storage
names, not a synthetic host filesystem. Java execution compatibility remains
separate from installation, as documented above.

A two-picture GUI queue was tested through the native importer: both entries
reached Imported, appeared in the right pane, and were saved in scratch NVM.
