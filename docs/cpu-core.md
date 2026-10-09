# ARM7TDMI core scope and verification

The interpreter in `src/arm7.c` is written locally in C17. No external CPU core
is used. Its little-endian bus is separate from per-model ROM/RAM configuration
and future device callbacks. Architectural reference: Arm's
[ARM7TDMI TRM, DDI 0210C](https://documentation-service.arm.com/static/5f4786a179ff4c392c0ff819).

## Implemented initial instruction families

| ARM | Thumb |
|---|---|
| All standard condition codes, data-processing ALU and barrel shifter | Immediate/register shifts, add/subtract, ALU ops, immediate ALU |
| B/BL/BX, PC pipeline operands | Conditional/unconditional branches, two-half BL, BX, high-register operations |
| MRS/MSR, SPSR return | SWI and common PC/SP-relative forms |
| MUL/MLA and signed/unsigned long multiply/accumulate | MUL |
| Word/byte/halfword and signed loads, pre/post indexing | Register/immediate load/store, signed loads, literal loads |
| SWP/SWPB | PUSH/POP, multiple load/store |
| LDM/STM with user-bank transfers and exception return | ARMv4T POP PC stays Thumb (does not behave as ARMv5 interworking) |

Modes: user/system share SP/LR; FIQ has banked R8-R14; IRQ/SVC/abort/undefined
have banked SP/LR and SPSRs. Implemented exception entry: SWI, externally
asserted IRQ/FIQ, and reset. FIQ takes priority over IRQ. IRQ/FIQ inputs are
currently core fields, not a guessed CX805 interrupt controller.

Word data loads implement ARM7 unaligned rotate-right behavior; stores align
down. Odd signed-halfword loads read/sign-extend a byte. PC operand pipeline
offsets, ARM PC stores, immediate zero shifts, RRX, register shifts of 32/33,
and old-bank writeback before LDM exception return have explicit tests.

## Deliberate limitations

- **Not cycle accurate.** `cycles` is presently a scheduler-tick counter:
  one instruction or interrupt entry increments it once. Bus wait states,
  prefetch pipeline timing and multiplier internal cycles are not modeled.
- No CP15/other coprocessor, MMU, JTAG or big-endian configuration.
- Invalid/reserved/unimplemented encodings and diagnosed unpredictable
  combinations halt with PC/opcode/reason. Undefined-instruction and hardware
  bus-abort vector entry are not yet implemented; do not mistake this
  diagnostic policy for full hardware exception behavior.
- Unmapped data reads set a bus fault and return zero; writes are logged and
  dropped. The frontend stops by default after that instruction. Explicit
  `--continue-unmapped` clears the diagnostic fault between instructions.
  Unmapped instruction fetch always stops, even in continuation mode.
- No instruction-level proof or exhaustive ARM compliance claim. Some
  architecturally unpredictable encodings are diagnosed; others still need
  review. Full instruction conformance and model-specific abort behavior are
  separate from the initial boot interpreter.

## Tests and real-image check

`build.cmd` builds and runs `tests/cpu_test.c` with `/std:c17 /W4 /WX /RTC1`.
211 assertions verify small instruction programs and invariants including
overflow/carry/borrow, failed condition codes, shifts, signed loads, unaligned
word loads, multiplication, block transfers, bank switching, PSR permissions,
SWI, IRQ/FIQ return, ARM/Thumb interworking and rejection of ARMv5-only forms.

The actual C100 reset vectors and bootstrap are also exercised: strict mode
stops at the first hardware read after three instructions. Diagnostic mode
executes 3.2 million instructions, completes initialized-data/BSS loops and
reaches a firmware hardware-validation failure loop without hitting an
unsupported instruction. This demonstrates progress through real startup,
not that all instruction corner cases or peripherals are correct.
