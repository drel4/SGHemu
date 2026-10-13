# C100 CPU/DSP tones, service LED and LCD feel

## Navigation sound (XEDK1)

`kid_keybeep` (`011443FC`) -> `audioStartTone` (`011D3DBA`) -> native tone
manager/timers -> `TGSetToneD1` (`012AF30C`). Two note bytes select entries in
`014C6440`: a Q15 oscillator cosine coefficient and an excitation/amplitude word.
Zero note means silence. `TGSleepD1` (`012AF50E`) stops this path.

The host observes these entries without skipping instructions, changing return
values or forcing callbacks. It renders two sine oscillators at 48 kHz using
the telephone DSP's inferred 8 kHz oscillator reference and the native table.
Output gain is provisional (four times the table excitation); complete DSP,
codec gain/route and speech processing are not implemented. A short gate ramp
reduces clicks. Native silent mode/key-tone selection still decides which calls
are emitted; musical key sounds that go through MA remain on the Yamaha path.

Without Yamaha, Windows waveOut outputs the CPU/DSP channel directly (no DLL
required). With Yamaha active, its worker mixes CPU tones into the same stereo
PCM blocks: a single twelve-slot, 10-ms output queue controls timing for both.
The former two-clock/queue setup is removed. Silence mixing is bit-exact and
active mixing saturates to signed 16-bit. Pause/reset control the shared output.
The GUI opens this output automatically for a configured model. Headless
execution is silent unless a diagnostic capture is requested.

Model declaration:

```
phone_io=0x012af30c,0x012af50e,0x014c6440,0x011465aa,5,3,6
```

This specifies start, stop, table, GPIO write entry and R/G/B GPIO indices.
Do not copy these addresses to another firmware without verifying them.

Regression: a read-only XEDK1 boot, Menu/Down/Down/Up/Back generated five native
start/stop pairs and 60,644 nonzero PCM samples. The native navigation note was
30 on both oscillators. `phoneprobe ... --cpu-audio-wav OUTPUT.wav [key IDs]`
captures this path without speakers or modifying the input NVM.
A unit regression also checks ROM tables behind flash callbacks: `bus_peek`
rejects device regions, so fast array-mode ROM needs a separate read-only lookup.

## Service LED

`control_alert_led` (`011ED550`) drives GPIO 5, 3 and 6.
The native factory LED-test callback (`011BECA6`) explicitly labels these as
Red, Green and Blue, confirming the channel order. GPIO writes are observed at
`uhGPIOWrite` (`011465AA`); native random-color selection, combinations and
blink timers continue to execute in the firmware.

MA melody LED events are separate from direct GPIO service-light control.
The firmware uses chip type 2 and emits MA-3 parameter `0146` with a one-bit
on/off value; no color is encoded in the melody. Previously these events became
unsupported NOPs. They now map to a private marker in the MA-5 stored no-op
parameter `01DE`, carrying an event serial and toggle. The existing DLL
sequencer executes it at the original delta; the worker reports its executed
value, rather than the host toggling early when the FIFO is filled.

For melody flashes, each on event chooses green, blue, red or yellow, following
the user's physical-phone observation; this is an HLE palette policy, not a
claim that those four colors are stored in SMAF. Newer CPU GPIO changes take
precedence over older melody events and vice versa. The native DLL regression
checks on after 100 ticks and off after another 10, with no early toggle.
A Bumer firmware run recorded 50 melody LED events.

The GUI updates its small and large program icons to solid colors matching
those three lines. Off is black. Eight reusable icon handles are released on
exit; no decorative LED frame, separate panel or host blink timer is added.

## LCD ghosting

**L&F -> LCD ghosting** enables a frontend-only exponential LCD
response with a provisional 75-ms time constant. It is off by default and saved
as `[look_and_feel] lcd_ghosting=0|1` in `state/emulator.ini`.

The native framebuffer, guest reads and LCD dumps remain exact and unchanged.
Only the displayed copy blends toward the current frame on the GUI refresh
timer; this continues after Pause so the display can settle. Toggling the effect
or loading another model initializes its history from the current frame.
