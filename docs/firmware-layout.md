# Firmware folders and model profiles

ROM sets live under `models/<model>/roms/<version>/`, which is gitignored.
Native GUI File > Open firmware folder selects an arbitrary existing folder;
ROMs need not be copied into the workspace. The scanner descends up to three
subfolder levels, ignores reparse points, and hashes at most 512 files no larger
than 32 MiB. File extension/basename is not used to assume a memory map.

Model descriptors `models/<model>/<version>.firmware` contain:

```ini
name=Phone and version
model=version-bringup.cfg
sha256_1=<64 hex digits>
sha256_2=<64 hex digits>
```

Both hashes must be present. Renamed images work; an incomplete pair, changed
image, unknown firmware or symbol listing alone does not boot. If several
complete profiles match, the GUI offers their names; the CLI reports all and
uses the first. Supported local C100 sets: XEDA2, XEDD1, XEDK1. XEDB2 currently
has only a symbol listing, not executable ROMs. New models require verified
maps/peripheral configuration and descriptors; importing an unknown folder
never guesses that another phone is a C100.

The XEDA2/XEDD1 profiles now share the GPIO/ADC battery and low-power timer
hardware with XEDK1 and provide their own relocated cellular, SMS, tone, import
and WAP service bindings. See [per-version verification and limits](c100-firmware-compatibility.md).

Model config paths are relative to the config file, not the process cwd.
Explicit import overrides are absolute. Configuration directives:

| Directive | Meaning |
|---|---|
| `name=text`, `reset=address` | Identity label and reset PC |
| `rom=base,path[,mapped_size]` | Load ROM; optional erased FF padding |
| `ram=base,size`, `alias=base,source,size` | Memory map; overlaps rejected |
| `display=width,height`, `audio=silent` | Host display and audio backend |
| `flash=base,manufacturer,device` | x16 flash interface on a mapped ROM |
| `nvram=base,size` | Optional snapshot window within a flash bank's padded tail; must not overlap supplied firmware bytes |
| `nv_provision=trigger_pc,format_pc,marker_pc` | First missing persistent save only: call verified Thumb guest factory formatter and marker writer at the initialized trigger, commit, cold reset; all entries must lie in supplied ROM |
| `flash_geometry=base,offset,size,erase_size` | Override erase size for a flash subrange; after `flash`, nonoverlapping, covering whole default 64 KiB sectors |
| `lcd_s6b33=command_port,data_port,origin_y` | Guest LCD interface/viewport |
| `intc=base`, `periodic_irq=bit,period` | Interrupt routing and diagnostic tick source |
| `ma3=base,irq_mask,sequence_quantum_ns` | MA-3 byte ports and interrupt source; model-specific divider time unit, 1157 ns inferred from C100 SetSpeed |
| `pmic=base,byte_address`, `pmic_register=index,value` | Observed serial pins and provisional initial PMIC state |
| `identity16=address,value` | Explicit read-only diagnostic input fixture |
| `register32=address,value` | Explicit writable latch/handoff fixture |
| `irq_gate=address,mask` | IRQ delivery enable bits in a mapped `register32` halfword; requires `intc`, leaves pending sources latched |
| `irom_service=address,memcpy` | Explicit Thumb internal-ROM memcpy entry; must not overlap mapped memory |
| `irom_version=address,halfword,return_pc` | Caller-specific Thumb version query; writes only the verified first halfword, rejects other callers; `FFFF` means unknown in C100 bring-up profiles |
| `irom_vector=address,thumb_target` | Read-only 32-bit indirect vector in the internal-ROM window; target must name a configured HLE service. Selector 18 uses version/memcpy vectors at 80030/8003C; these alias existing HLE contracts, not dumped ROM bytes |
| `intc_aux=base` | Optional second interrupt bank sharing the CPU's IRQ/FIQ lines |
| `charger_complete=port,done_bit,enable_bit,bank,irq,settle_ms` | GPIO charger-completion subset; active-low enable, done held until unplug/SOC drop, bank 0 main or 1 auxiliary |
| `diagnostic=continue` | Bring-up profile allowing logged unknown MMIO |
| `unplaced_rom=path` | Legacy retained image without a guest mapping |

The shared CPU is independent of model configs. Peripheral register/pin
locations and initial states belong to model profiles. Private firmware/DLLs,
voice files, listings, flasher S-records and generated images are never committed.
