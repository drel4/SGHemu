# C100 filesystem investigation

The native **File > Phone filesystem manager...** inspects the live phone
directory. It can refresh, export directory metadata as TSV, and back up NVM. It
does **not** yet export file contents, and it does not import. The CLI lists with
`--fs-list FILE.tsv` after running the guest and imports a downloaded sound with
`--import FILE` (see [the content import record](c100-content-import.md)).

## Verified directory layout

`flash_SearchAll` is at XEDK1 `0x011643B2`, XEDA2 `0x011642FE`, and
XEDD1 `0x01164406`. Matching code and literal pointers establish the shared
RAM table at `0x0067805C` and header at `0x00678D88`.

The header contains a halfword slot count (100) and NLT record count (1).
Each slot is 28 bytes:

| Offset | Field |
|---|---|
| 0x00 | NUL-terminated name, 12-byte field |
| 0x0C | Halfword file ID; zero means inactive |
| 0x0E | Type byte |
| 0x10 | Monotonic allocation serial the flash task assigns; never surfaced to readers |
| 0x14 | File size |
| 0x18 | Additional metadata, not fully decoded |

The inspector reads ordinary mapped RAM directly, never MMIO or the bus access
functions. It rejects uninitialized headers, unsupported layouts, malformed
names and duplicate IDs. The model config supplies table/header/capacity/stride.
The verified initialized XEDK1 fixture lists 23 records, totaling 110,050 bytes.
These include system settings and Java metadata: this total is **not** user
space usage, and is not evidence for the handset's advertised free-space quota.

## Import path

XEDK1 `flash_aWriteByName` at `0x011642C0` queues asynchronous writes. Return 6
means accepted, not committed. Its descriptor retains a guest data pointer,
ticket and completion callback; imports must keep the buffer alive until the
flash task has consumed it. `src/content.c` makes the guest run this call itself
and is documented in [the content import record](c100-content-import.md).

`iflash_WriteEntry` (`0x0121FCB4`) and `iflash_ReadData` (`0x012201DA`) traverse
logical UID/chunk chains. A filesystem file is not a flat range of NVM bytes, but
a file created through the guest's own writer is internally consistent by
construction. `EasyImageWriteData` (`0x0117D658`) uses file type 7;
`File_CreateObj` (`0x01232A6E`) creates the 2,740-byte `objfile` media catalog with
20 entries. `JAVAFileCreate` (`0x011439CC`) creates type-10, 3,500-byte metadata
records; writing a JAR as an arbitrary file does not install a MIDlet. Picture
imports now convert to the native RGB565 container before invoking the type-7
writer. Java imports run the actual Jbed installer, which creates its own
type-10 files and suite registry; see the content-import record.

Raw flash patching or copying host files into the model directory is not a
working substitute for these guest storage/catalog/install operations.
ROMs, NVM snapshots and raw decompilation remain ignored private artifacts.
