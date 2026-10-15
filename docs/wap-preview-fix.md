# Downloaded-MMF preview freeze

This was a guest-phone freeze, not a Windows process crash. XEDK1 firmware and
the actual pinned Yamaha hardware DLL were exercised using read-only NVM/SIM
snapshots, the native WAP browser, real HTTP downloads and the phone's Play?
confirmation. No firmware, DLL or user snapshot is included here.

## Cause

Host-injected NetDataArrived callbacks borrowed the idle CPU SP (`0066C49C`).
That stack overlapped parked task frames. IRQ preemption could overwrite a saved
return address, stopping at low RAM (`000000DC`). Deferring IRQs alone was
insufficient: deep ordinary WAP stack writes overwrote a parked saved CPSR at
`0066C064` with transaction ID `00007710`. Preview subsequently restored that
invalid context and stopped at `012D4604` with SP/LR zero.

## Fix

Allocate a private 16-KiB guest stack for injected calls; validate that allocation
is wholly writable plain RAM and restore the original CPU context afterward.
Defer interrupt delivery while a native transaction is active, preserving device
latches and advancing emulated time. Do not begin a transaction ahead of a
pending unmasked interrupt. Logging now records CPU-stop reason and registers,
and includes WAP transport tracing when File -> Logging is enabled.

## Verification

- Cellular tests: 8,321 checks, including deep callback stack writes, 4,096-byte
  parked-stack canaries and pending IRQ/FIQ entry guards.
- Machine tests: 163 checks, including deferred IRQ delivery and retained pending
  interrupt status.
- Native peer tests: 71 checks; the firmware's 30-byte receive ABI is preserved.
- Full Release build and CPU differential tests (246,608 cases) passed.
- Native browser downloaded `133.mmf`, accepted Play?, and returned to Save?
  without stopping. No audible-output claim is made for that file.
- The same download/preview flow for `Bumer-Zapravka.mmf` produced Yamaha PCM
  (nonzero frame count rose from 626,180 to 1,841,912), returned to Save?, and left
  the CPU running with no Yamaha failure. This tests the previously freezing path,
  not merely direct file playback through a host player.

Visual ghosting regressions are covered separately by GUI tests: partial LCD
clears are not published to the response filter before the 3-ms quiet interval.
