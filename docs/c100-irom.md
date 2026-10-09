# C100 internal-ROM callers and first no-SIM UI

## Evidence, not guessed BIST flags

The supplied external firmware does not contain the internal-ROM service bodies.
It does contain their wrappers and callers, so their *consumed* contracts can
be recovered. Ghidra 12.1.2 headless decompilation used the XEDK1 BIN1 image at
`01000000`, language `ARM:LE:32:v4t`, and its supplied linker listing. Results
were cross-checked against Thumb instructions and the other two firmware sets.
The reusable RE script is [C100Irom.java](../tools/ghidra/C100Irom.java).
Generated decompilation and project files stay under ignored `build/`.

`IromBistResults` wrappers are at XEDA2 `01388A36`, XEDD1 `0138969A`, and
XEDK1 `0138973E`. With the observed selector `15` at `00002FFA`, their target
is Thumb `00081FE1` (execution PC `00081FE0`). The only identified direct
caller is `UHGetIromVersion`, `0111AACC` in all three images:

1. Allocate stack outputs; pass R0 = SP+8, R1 = SP+4, R2 = SP.
2. Call the wrapper at `0111AAD4`; return PC is `0111AAD8` (Thumb LR `0111AAD9`).
3. Read **only the halfword at the first output**. Discard the other outputs.
4. Convert it to three hex digits in the string buffer at `0040C265`.

Its direct caller in `mmi_VersionInit`, `0100234E`, formats `Irom : %s` for
the version-information screen. There is **no BIST success/failure branch**
in this observed use, and no consumed scalar result. Decompiler guesses about
parameter counts or indirect control flow were not treated as authoritative.

The real internal-ROM version is still unknown. Bring-up profiles report
`FFFF`, displayed as `FFF`, explicitly as an unknown diagnostic value. This
is not proof that the real service returns `FFFF`, or that a hardware test passed.

## Narrow implementation

[irom.c](../src/irom.c) implements two configured HLE entry points:

| Entry PC | Implemented contract |
|---|---|
| `00081FE0` | Only the verified version-text caller: write the first halfword; leave other outputs untouched; reject a different LR |
| `0008077C` | memcpy: R0 destination, R1 source, R2 byte count; return destination |

Both require Thumb entry and return with ARMv4T BX-LR interworking. Flags and
callee registers are preserved. Memory ranges are checked before mutation;
MMIO destinations, read-only destinations, wrapping ranges and overlapping
memcpy buffers are rejected. Entries cannot shadow mapped ROM/RAM. Interrupt
delivery takes precedence over an HLE call. One atomic scheduler step per call
is a bring-up approximation, not a cycle-accurate internal-ROM implementation.
73 synthetic checks cover the contracts, invalid inputs, configuration and IRQ
priority. Full build: **629 checks**, MSVC x86 C17 `/W4 /WX /RTC1`.

## Other identified wrappers

The XEDK1 listing identifies these 13 wrappers. This is not an exhaustive proof
against indirect internal-ROM calls, nor evidence all are required for startup.

| Wrapper | XEDK1 address | Status |
|---|---|---|
| DoRAMTest | `01389686` | Not implemented |
| DoCRCTest | `013896B4` | Not implemented |
| DoToggle | `013896E2` | Not implemented |
| GenerateCRC | `01389710` | Callers decoded; algorithm not yet established |
| IromBistResults | `0138973E` | Version-text use only, not full BIST |
| IrFrameSearch | `0138976C` | Not implemented |
| IrCreateFrame | `0138979A` | Not implemented |
| memcpy | `013897C8` | Implemented |
| gprsComputePPP16bitFCS | `013897F6` | Not implemented |
| gprsPutHDLC | `01389828` | Not implemented |
| gprsCheckPPP16bitFCS | `0138985A` | Not implemented |
| gprsCopyPPPRawCirc | `0138988C` | Not implemented |
| gprsDecodePPPHDLC | `013898BE` | Not implemented |

IrDA `GenerateCRC` callers consume a buffer and byte count, then append the
low 16-bit result in little-endian order. Polynomial, initial value and final
transformation remain to be verified; an arbitrary CRC substitute is not used.

## Guest-rendered Insert SIM Card screen

With the local, guest-generated XEDA2 factory snapshot described in
[NVM analysis](c100-nvm.md), a fresh CPU/RAM boot now passes the version query
and renders **Insert SIM Card**, signal and battery icons. At 100 million
instructions the screen is clean white with no remaining GPRS splash pixels:

- 124,814 LCD pixel writes and 47 commands, all from guest execution.
- One version-query HLE call; zero memcpy HLE calls.
- 277 flash programs and zero erases; original ROM files unchanged.
- 244,525 logged unmapped/ROM faults in diagnostic continuation mode.

Local capture: `build/c100-after-version-hle.bmp`; log:
`build/c100-after-version-hle.txt`. These private artifacts are not distributed.

```powershell
.\build\sghemu.exe models/c100/c100xeda2-bringup.cfg 100000000 --nv-load build/c100-xeda2-factory-diagnostic.snv --lcd-out build/c100-after-version-hle.bmp
```

The frontend now provisions missing saves with guest format-and-marker routines
and persists NVM automatically. Open a supported ROM folder; no manual snapshot
selection is necessary. Existing snapshots can still be explicitly imported.
See [current storage behavior](c100-nvm.md). This GUI workflow compiles; the
new menu actions have not yet been exercised through desktop automation.

This is a real firmware-rendered no-SIM UI, not completed phone emulation.
Guest keypad, SIM/radio behavior and MA-3 audio still need implementation.
Factory cable transport remains unimplemented; frontend first boot invokes
verified guest service entries directly. The earlier diagnostic factory flag
and UI argument overrides were not added to the runtime.
