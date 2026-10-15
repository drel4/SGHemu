# C100 firmware compatibility

XEDA2, XEDD1 and XEDK1 use the same emulated C100/CX805 peripherals. They do
not need separate CPU, battery, GPIO, SIM, RTC, MA or low-power timer models.
The older profiles now enable the shared hardware rather than supplying a
fixed charger input and leaving ADC/wakeup MMIO unmapped.

Some remaining high-level integrations still call firmware services. Their
code addresses must be verified separately for each executable, even on an
identical phone. A single offset does **not** relocate an entire firmware.
For example, XEDA2's allocator moves backwards, its Java entry moves forwards,
and its WAP state moves in RAM. The three hash-matched profile descriptors
continue to select explicit configuration, not a runtime address guess.

## Verified bindings

Addresses below are even Thumb entry PCs or data addresses, as in the configs.

| Binding | XEDA2 | XEDD1 | XEDK1 |
|---|---|---|---|
| HWStandby | `01150F7C` | `01151064` | `01151010` |
| GSMMalloc | `0115371A` | `01153802` | `011537AE` |
| GSMFree | `01153936` | `01153A1E` | `011539CA` |
| GSMSendUIMessage | `01146A44` | `01146B2C` | `01146AD8` |
| GSMSendMessage | `011543C2` | `011544AA` | `01154456` |
| SendUIMNSetupReq | `012546E4` | `01255200` | `012552A4` |
| GSMGetPLMNName | `0125511C` | `01255C38` | `01255CDC` |
| serviceTable | `007AD174` | `007AD1B0` | `007AD1B0` |
| SendSRSMDataReq | `01301414` | `01302078` | `0130211C` |
| EventProcessEvents | `01222198` | `01222B80` | `01222BB4` |
| WAP UP_GlobalData | `00788AA0` | `00788ADC` | `00788ADC` |
| WAP driver state | `00788BC0` | `00788BFC` | `00788BFC` |
| TGSetToneD1 | `012AE604` | `012AF268` | `012AF30C` |
| TGSleepD1 | `012AE806` | `012AF46A` | `012AF50E` |
| ToneTable | `014C40C8` | `014C63FC` | `014C6440` |
| Java Netopen | `01166B94` | `01166CFC` | `01166CA8` |
| Java DSGetHostByName | `01159472` | `0115955A` | `01159506` |
| Java OnAsyncNetworkEvent | `0112620C` | `0112620C` | `011261E8` |

Entry points were compared against the locally supplied XEDK1 linker symbols
and disassembly. Matching accounts for relocated BL targets, PC-relative literal
loads and pointer pools. Literal-load pairs establish moved globals; in particular,
GetServiceId loads serviceTable + 0x40 then subtracts 0x40. GSMGetPLMNName has
a changed PLMN-table search bound, not a different ABI. The duplicate small
GSMSendUIMessage-style wrappers were resolved using their containing code block
and exercised through the native incoming-message path.

The profiles also contain relocated media/Java import and WAP bindings. Native
Java application compatibility remains dependent on the game and handset;
adding these bindings is not evidence that arbitrary MIDP games work.
The [Java DNS/TCP bridge](java-network.md) also has separate bindings for each
version. XEDK1 passed a native network installation; XEDA2/XEDD1 bindings were
disassembly-checked and the updated profiles boot-tested, but their full native
network installation remains unverified.

## Reproducible integration check

The Release build produces an optional `c100_compat_test.exe`. It needs locally
supplied ROMs and a **new** state directory; it refuses an existing directory.
It never loads or overwrites model-local/user snapshots.

```bat
build\c100_compat_test.exe models/c100/c100xeda2-bringup.cfg local/compat-new-a local/compat-a
build\c100_compat_test.exe models/c100/c100xedd1-bringup.cfg local/compat-new-d local/compat-d
```

The test performs the firmware's own first-boot formatter, marker writer and
defaults reset, waits for network registration, inspects the live user directory,
delivers SMS and an incoming call, dials from the keypad, accepts/hangs up at the
host end, and changes physical battery/charger inputs. It saves
LCD screenshots under the requested prefix. Firmware, screenshots and state
remain local, not redistributable test fixtures.

The first XEDA2/XEDD1 pass reached standby after two automatic resets, listed 26
default user files, displayed the SMS sender and incoming caller number, and
completed the charger/ADC check (52 conversions and nine cellular events).
The extended pass also dialed `123` through the emulated keypad and accepted /
hung up the outgoing call. Both versions finished with 13 delivered events,
64 ADC conversions and 26 native CPU-tone changes, without a CPU halt.

## Remaining limits

The updated 40-pin GPIO driver-selection fixture was additionally checked on
all three profiles with fresh NVM initialization, automatic resets, keypad
dialing, host accept/hangup, SMS, incoming calls and battery ADC input. Read-only
charger-at-reset runs at 100% reached the native hatched-chick screen on all
three; a 25% control remained in charging mode. Startup MA audio on XEDK1
matched the previous dry baseline after a 2-ms onset alignment.

- These are C100 profiles, not support for other Samsung models.
- XEDB2 has a linker listing locally but no executable pair to boot/verify.
- RF, speech, a real baseband/DSP and complete charger electronics are not emulated.
- Battery voltage and charger completion are separate signals. The shared
  [charger controller](c100-battery.md) now supplies completion after a settling
  delay; the silicon identity remains an inferred fixture, not a measured ID.
- New profiles do not remove the existing limits of Java, WAP or media import.
- Java/media import bindings were relocated, but complete end-to-end import/Java
  verification on these two versions remains outstanding.
- WAP virtual-bearer setup now retains queued URL text outside the edit buffer;
  the gateway correctly answers session Resume. XEDD1 was verified loading the
  requested external directory and reopening to a second URL. XEDK1 also passed
  a distinct second-URL fetch after Resume with the updated hardware profile;
  XEDA2 fetched and rendered the directory after normal gateway setup. A network
  icon alone is still not evidence of packet exchange.
