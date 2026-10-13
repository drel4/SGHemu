# C100 RTC and host synchronization

## Evidence

XEDK1 `RTCRead` (`0115EC04`) reads binary counters from `00040080`.
Existing unmapped-access logs confirm reads at `40080`, `40082`, `40084`,
`40086`, `40088` and `4008A`. `RTCWrite` (`0115ECD0`) clears the calendar
through `+0x0C`, then writes **one repeatedly** to each counter to increment it;
these are not direct-assignment or BCD registers.

The RTC device descriptor supplies year origin **1996** (`0x07CC`). The exact
`40080, 1996` literal pair occurs at ROM offset `00120C18` in all three locally
verified C100 first-bank images: XEDA2, XEDD1 and XEDK1. Year reads are masked
to five bits by the driver, limiting this contract to 1996..2027.

| Offset | Value exposed to firmware |
|---|---|
| `00` | Seconds, 0..59 |
| `02` | Minutes, 0..59 |
| `04` | Hours, 0..23 |
| `06` | Day minus one |
| `08` | Month minus one |
| `0A` | Year minus configured origin |
| `0C` | Calendar-reset/control register |
| `12..1A` | Alarm comparison registers; disabled value `FFFF` |

## Implementation

`src/rtc.c` implements the counter interface, independent increment writes,
Gregorian calendar rollover and a 32-year hardware year wrap. Models declare
`rtc=0x00040080,1996`; no C100 addresses are hardcoded into the shared CPU.
Ordinary deterministic CLI runs use emulated nanoseconds, not host time.
Alarm registers retain writes/readback; alarm interrupt delivery is not yet
implemented. Battery-backed reset metadata beyond this 32-byte register block
is not fabricated or mapped by the RTC module.

The frontend's host-sync mode supplies `GetLocalTime` samples once per second,
including while paused, under the existing machine mutex. The RTC uses that
cached local civil time without advancing it according to CPU speed. Firmware
calendar resets/writes cannot override the clock while synchronization is on;
turn it off to use the phone's Set Time/Date controls. Alarm writes remain
independent. Synchronization never patches guest code or its cached date RAM.

GUI synchronization defaults on for supported models and persists in
`state/emulator.ini`, `[emulation] sync_time`. CLI `--rtc-sync` explicitly
enables it; the CLI updates the same host source periodically during execution.
`phoneprobe --rtc-sync` can capture the result without saving input NVM.

The separate **High process priority** menu option requests
`SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)`; disabling it
requests `NORMAL_PRIORITY_CLASS`. It does not change guest PLL frequencies,
instruction timing, or request Real-time scheduling. It defaults off and
persists as `[emulation] high_priority` in the same preference file. Windows
rejections leave the UI option unchanged and report the error.

## Verification

Release `/O2 /MT`, x86, C17 `/W4 /WX`: all suites passed, including 34 new RTC
checks covering register encoding, native counter writes, host-write protection,
mode switching, leap days, date validation, midnight and year rollover.

A real XEDK1 firmware boot with initialized read-only input NVM and
`--rtc-sync --high-priority` reported Windows process priority **High**. Its
native standby LCD showed **21:39, Tuesday 13 October** while Windows reported
**2026-10-13 21:39**. Captures/logs remain ignored under `build/rtc-synced.*`;
firmware and existing NVM were not overwritten.
