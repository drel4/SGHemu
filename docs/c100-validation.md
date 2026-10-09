# C100 hardware-validation path: next bootstrap stage

Research on 2026-10-09. Evidence: local C100XEDA2 first image,
[Ghidra listing](c100-validation-ghidra.txt), and the optional real-image
`build/c100_identity_test.exe`. No firmware bytes or instructions were patched.

## Call chain

```text
0x0111F3E6 -> 0x0115C81A
  -> 0x01122ECC: return cached class byte at 0x0040CD7C
       (classify with 0x01122D9E if zero)
  -> 0x0115C740: find class in four zero-terminated lists
       and copy 22 function pointers into RAM at 0x00676EE0
```

`0x0115C740` returns zero if no list matches. The caller then enters the
previously observed self-loop at `0x0111F3F0`.

The classifier reads:

| Address | Getter | Role inferred from comparisons |
|---|---|---|
| `0x00040294` | `0x01122CEA` | Family field (`0x4700`, `0x4800`, `0x0F00`) |
| `0x00040296` | `0x01122CF0` | Additional field used by the `0x4800` branch |
| `0x00040298` | `0x01122CF6` | Part/revision field (`0x0202`, `0x0303`, etc.) |
| `0x00002FFA` | `0x01122CFC` | Selector field, also read at reset |

These are behavioral descriptions. Actual CX805 register names and the correct
C100 revision values are **not confirmed**. They must not be treated as a
complete register model or silently installed into the default C100 config.

## Accepted classes

For family `0x4700`, the classifier accepts these pairs:

| `0x40298` | `0x2FFA` | Class |
|---|---|---:|
| `0xFFFF` | `0x15` | 2 |
| `0x0202` | `0x15` | 3 |
| `0x0202` | `0x16` | 4 |
| `0x0303` | `0x16` | 5 |
| `0x0404` | `0x16` | 6 |
| `0x0606` | `0x16` | 7 |
| `0x0909` | `0x16` | 8 |
| `0x0909` | `0x19` | 9 |
| `0x0C0C` | `0x16` | 10 |
| `0x0C0C` | `0x19` | 11, possibly refined to 12 |
| `0xFFFF` | `0x11` | 15 |

Family `0x4800` becomes class 13 when `0x40296 == 0` and selector is `0x18`,
otherwise class 1; class 13 can be refined to 14. Family `0x0F00` becomes
class 17 when `0x40298 == 0xFFFF`, otherwise class 16. Refinement depends on
the additional probing function `0x01122D02`, not just the four fields above.

The driver-table arrays are stored at:

| Class list | Driver table | Accepted classes |
|---|---|---|
| `0x013D3C98` | `0x013D3CB0` | 1, 2, 3, 4, 5, 6, 7, 15 |
| `0x013D3CA1` | `0x013D3D0C` | 8, 9, 10, 11, 12 |
| `0x013D3CA7` | `0x013D3D68` | 13, 14 |
| `0x013D3CAA` | `0x013D3DC4` | 16, 17 |

Some driver entry points live in bootstrap-copied low RAM, for example
`0x00400075` (Thumb). This is additional evidence for keeping model memory
mapping separate from CPU instruction decoding.

## Explicit experimental profile

`models/c100-lab.cfg` configures **read-only identity fixtures** for the accepted
class-3 tuple: family `0x4700`, extra field zero, part field `0x0202`, selector
`0x15`. It does not assert that these are the real phone's values. Selector
`0x15` avoids the special reset paths that jump into the still-unmapped
possible internal boot ROM for `0x16`/`0x11`.

The new `identity16=address,value` model entry maps only those two bytes;
byte reads work, writes fault/log and do not change the value. It is not a
general-purpose unknown-MMIO latch. Default `c100.cfg` has **no identity
fixtures** and still stops at its first unknown hardware read.

Strict execution of the experimental profile stops at the next unknown write,
`0x00040340`. With explicit diagnostic continuation:

```powershell
.\build\c100_identity_test.exe
.\build\sghemu.exe models/c100-lab.cfg 3400000 --continue-unmapped
```

The optional regression stops at `0x0115C828` after 3,056,521 instructions:
`R0=1`, `R4=3`, cached class byte `3`, first RAM driver-table pointer
`0x01217D01`. Thus the real firmware classifies the tuple and initializes its
driver dispatch table; there is no forced success return or patched branch.
This is an exploratory hypothesis, **not a validated phone boot**.

## Newly exposed accesses

The class probe touches `0x00040D10`, `0x0004090E`, and reads at
`0x00109006`. Later initialization touches `0x00040902`, `0x00040108` (32-bit),
`0x00040B04`, `0x00040B00`, and `0x00040B02`. Their purposes and status bits
remain to be determined. At a 3.4-million-instruction budget the diagnostic
run is around `0x011581A6`, with 1,344 unknown accesses, including a scan beyond
the currently mapped low-RAM bootstrap span. This is not a normal idle loop
claim and is not used to invent further RAM/peripheral behavior.

Next: obtain independent evidence for the actual identity tuple, RE the probe
at `0x01122D02` and the selected clock/initialization functions, establish
register readback/status semantics, then model those peripherals rather than
discard their writes. LCD and keypad addresses are still unidentified; the
new frontend therefore exposes blank pixel storage and host key state only.

## Subsequent visible boot

The final paragraph above describes the earlier identity-only investigation. New linker listings established the second ROM mapping, LCD ports, PMIC serial pins and interrupt routing. Experimental bring-up profiles now reach the firmware GPRS splash. See [LCD/boot evidence](c100-lcd.md) for the inferred handoff and fixed-instruction IRQ source, still-unconfirmed hardware identity, and remaining peripherals. Keypad buttons still record host state only.
