# C100 keypad

The bring-up profiles expose the guest-scanned active-low matrix at `0x00040800`:
control +0, row drive +2 (high byte), column sense +4. XEDK1 KPHandler scans
eight rows/four columns and performs its own debounce. Raw scan codes are
`(row << 5) + column + 1`. PeriodicSystemServices calls KPHandler; a separate
keypad wake interrupt and electrical ghosting are not yet modeled.

## Corrected controls

The initial map incorrectly treated Samsung's logical key codes as Windows
virtual-key codes. In particular `0x26` is Clear, not Up. XEDK1's
`kid_HandleClear` at `0x011af464` deletes a character for `0x26`, while
`kid_HandleMove` at `0x011af8aa` moves up/down for `0x24/0x25` and left/right
for `0x44/0x45`. `kid_ManageInput` at `0x011aefc4` selects the two scene soft-key
slots for `0x47/0x48`. Decoding is performed by guest `kid_KbdParser`, not by
host UI event injection. Configs for all three supported firmware versions use:

| Host button | Row, column | Raw scan | Normal logical code |
|---|---|---|---|
| Soft left | 7, 0 | E1 | 47 |
| Soft right | 7, 2 | E3 | 48 |
| Up | 7, 1 | E2 | 24 |
| Down | 5, 1 | A2 | 25 |
| Left | 6, 0 | C1 | 44 |
| Right | 6, 2 | C3 | 45 |
| Call | 5, 0 | A1 | 27 |
| End | 0, 3 | 04 | 28 |
| C/Back | 5, 2 | A3 | 26 |

OK/Enter is explicitly a host convenience alias for the left soft-key switch,
not a claimed additional physical C100 key. Aliases have independent held bits:
releasing one does not release the switch while another alias is held.
The numeric rows remain unchanged: 1–3 row 4, 4–6 row 3, 7–9 row 2,
and */0/# row 1, columns 0–2 respectively. Guest context/long presses can change
logical codes; no firmware table is patched.

## Reproduction

`tools/ghidra/C100Nvm.java` accepts individual function names and `depth:0` for
focused decompilation. The linker listing supplies the XEDK1 function names.
Generated decompilation and firmware-derived screenshots remain in ignored build/.

The optional real-ROM test loads a completed XEDK1 NVM snapshot read-only,
checks all 22 host mappings against the running guest's key table, then presses
digits, Clear, End and both soft keys through the matrix and captures LCD output:

```powershell
.\build\c100_keypad_test.exe PATH-TO-COMPLETED-XEDK1.snv
```

Synthetic `keypad_test` checks scanning, byte accesses, invalid accesses,
simultaneous keys, alias release and focus-loss-style release of all keys.

Local validation passed 78 real-ROM checks: the guest emitted `31`, `32`, `26`,
`28`, `47`, `48` for 1, 2, Clear, End and the soft keys. LCD captures confirm
Clear changed 12 to 1, End returned to Insert SIM Card, and the right soft key
opened the Call SOS confirmation. All 678 synthetic checks also pass.
