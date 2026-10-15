# C100 virtual cellular carrier, SIM storage and WAP

## User controls

The C100 XEDK1 bring-up profile supplies firmware-specific bindings. Other
models/firmwares must have their own recovered bindings before these controls
are enabled; addresses are not shared across firmware versions.

**Emulation -> Cellular** provides:

- **Enabled**: local synthetic registration, independently of packet data.
- **Network name...**: default Tele2; 1-30 printable ASCII characters.
- **Send SMS to phone...**: numeric sender and a single UCS-2 message of up to
  70 characters. Surrogate pairs/emoji and multipart SMS are not supported.
- **Call phone...**: an incoming call with a chosen caller number. Answer or
  reject it with the phone's own controls.
- **GSM -> Off / Internet / Internet + built-in WAP gateway**: packet-data mode.

When the phone dials a number, a host **Incoming call from phone** window offers
Accept/Decline. Accepted calls have a Hang up button; ending the call on the
phone closes the host window. These are local simulated endpoints, not real
operator calls or SMS. Speech audio, GSM RF and subscriber authentication are
not implemented.

Selecting the built-in gateway displays setup instructions. In the phone's WAP
browser profile, enter **10.0.0.1**, **Non-secure**, port **9201** if the port is
editable. Use **http://sghemu.local/** as an offline test homepage. The emulator
does not rewrite saved WAP profiles. Cellular must also be enabled. On the C100,
open Menu 7.2.4 (Browser settings), choose a profile, and use **#** to enter dots
in the IP address. The Non-secure option can be labelled Open channel. A blank
homepage also opens the built-in landing page.

Preferences are stored in `state/emulator.ini`, under `[cellular]`:

```ini
enabled=1
network=Tele2
gsm_mode=0
```

## SIM and message persistence

The virtual SIM advertises SMS and SMS-parameter storage, with EF_SMS
`6F3C` (ten 176-byte records), EF_SMSP `6F42`, and EF_SMSS `6F43`.
The layout follows the SIM file definitions in
[GSM 11.11 / ETSI TS 100 977](https://www.etsi.org/deliver/etsi_ts/100900_100999/100977/08.14.00_60/ts_100977v081400p.pdf).
The synthetic service-centre address is +00000000001; it does not connect to a
real SMS centre.

SIM contacts, SMS, own-number and message parameters are stored beside the
firmware-specific phone NVM: `<firmware-hash>.snv.sim`. GUI and persistent CLI
runs load and save them automatically. Card-protocol reset preserves the EFs;
machine reset reloads the snapshot. The snapshot is identity-bound to IMSI and
ICCID, CRC-checked, and atomically replaced. Invalid snapshots are rejected
before any EF changes and are not overwritten after a failed load.

Incoming host SMS enters the **native SR task**, not a fabricated inbox entry.
The firmware decodes the TPDU, chooses SIM/phone storage, performs its own
APDUs/NVM writes and presents its normal notification and message UI. Outgoing
SMS goes through the native editor/encoder; the carrier acknowledges the native
send request so the phone can save a sent record and show Message sent.

## Recovered XEDK1 boundaries

All function entries below are Thumb addresses, stored without bit 0.

| Boundary | Address | Adapter role |
|---|---|---|
| Idle scheduler entry | `01151010` | Safe point for queued guest calls |
| GSMMalloc / GSMFree | `011537AE` / `011539CA` | Native event ownership |
| GSMSendUIMessage | `01146AD8` | Registration/call indications |
| GSMSendMessage / SR task | `01154456` / `00672CDC` | Native SMS indications |
| SendMMSelInd | `01276C58` | Synthetic home registration instead of absent RF |
| GSMGetPLMNName | `01255CDC` | Configured virtual operator name |
| Native service table | `007AD1B0` | Voice capability 0; do not use capability 1 |
| SendUIMNSetupReq | `012552A4` | Outgoing call endpoint |
| Connect / disconnect requests | `012552FC` / `012552D0` | Answer/end calls |
| SendUIMNAlertReq | `01255328` | Consume local alert acknowledgement |
| SMS data request / response | `0130211C` / `01302132` | Send completion / receive acknowledgement |

Incoming calls require **SetupInd `163` followed by AlertingInd `162`, type 1**.
Setup alone only caches the native call offering; it does not display a ring.
Outgoing alerting uses type 0. Connect completion is `160`; disconnect is `15E`.
SMS enters SR through DataInd `1A0`; successful send completion is DataCnf `1A1`.
These values were verified against decompilation and actual instruction bytes
where the decompiler incorrectly truncated memcpy-containing functions.

`src/cellular.c` queues native allocations/messages/callbacks from an idle
safe point, preserves the interrupted CPU register/banked state and instruction
counts, and avoids overlapping native content/battery injections. It does not
draw handset screens. Model loading validates bindings against mapped ROM/RAM.
Startup registration can begin after the native startup version query and SIM
activity, rather than waiting unconditionally for 300 million instructions.
Queue polling runs only at the native injection-safe entries.

The GUI, CPU interpreter, Yamaha DLL and network worker have separate threads.
The LCD paint path uses a GUI-owned snapshot and never waits for the CPU mutex;
refresh timers skip busy slices after a bounded wait. CPU slices use a 2 ms wall
budget, and unpaced execution no longer adds a 16 ms sleep after each slice.
Instruction execution stays ordered on one CPU thread; this is not a parallel
ARM interpreter. Audio backpressure still supplies pacing when output is active.

## Experimental packet-data bridge

`src/phone_net.c` bridges recovered native WAP transport calls to a dedicated
Windows networking thread. Internet mode forwards native UDP datagrams to the
configured external gateway; it does not provide an external WAP gateway itself.
Built-in mode handles non-secure WAP traffic on UDP port 9201 locally and forwards
other UDP traffic normally. One gateway endpoint is tracked per bearer.
Selecting Internet makes the bearer available, not connected: the host worker
starts only when the firmware requests a transaction. While no receive
notification is pending, the CPU skips the queue lock. A synchronized pending
flag rearms delivery when subsequent replies arrive.

The small C gateway uses the WAPWarp project's WTP/WSP framing approach without
requiring Python at runtime. It supports class-2 requests, duplicate-request
caching, bounded transactions, segmented results, acknowledgements, retries and
GET/HEAD. Windows WinHTTP supplies HTTP/HTTPS retrieval. Text/HTML/basic WML is
converted to a simple WBXML card with text/links; already-compiled WMLC and small
binary files can pass through. Converted text/HTML uses independently fetchable
**Prev/Next** pages capped at **1,200 compiled bytes**, including navigation.
Each page fits one current WTP Result; packet segmentation alone does not reduce
the browser's compiled-document size. Link labels retain their original text
instead of displaying duplicate absolute URLs. Relative paths (including `../`)
are resolved while preserving case and escaped bytes. Oversized text is split
on UTF-8 boundaries, and links remain atomic except for oversized labels.
The private `__wapwrap_page` query selector is removed before upstream fetching;
other query parameters remain intact. Invalid selectors receive HTTP 400 and
out-of-range pages 404. Pages are rebuilt from the current upstream document.
`tests/wap_paging_test.c` checks all pages of a large listing, link completeness,
query preservation, Unicode strings and WBXML structure. Its optional URL
argument checks a live HTML listing with the same converter. On 2026-10-14,
`dumpster.legacy.ralsei.tech` directories `drel`, `flfx` and `ltbr` converted to
6, 2 and 5 bounded pages respectively.
The XEDK1 firmware browser was also tested via a temporary local landing-page
fixture (not included in the normal GUI): it opened the actual `drel` URL,
rendered Page 1/6, followed Next to Page 2/6, and returned via Prev. The input NVM/SIM snapshots
were read-only during the probe. Completed WTP replies are evicted oldest-first
when the small cache fills, so rapid paging does not exhaust transaction slots;
outstanding transfers are never evicted.

Large responses are bounded; HTML scripting,
forms/POST, WTLS, push, complete WML semantics and Java TCP networking are not
implemented. This is a high-level data bridge, not emulated GSM RF/IP hardware.

XEDK1 transport bindings (`wap=` in the model config):

| Native entry/state | Address |
|---|---|
| DevTxnStarted | `0120ECA2` |
| DevSendPDU / DevRecvPDU | `0120F140` / `0120F3E6` |
| DevGetBearerState | `0120F5B6` |
| EventPostEvent / NetDataArrived | `01222B32` / `012D466A` |
| WAP / driver state bases | `00788ADC` / `00788BFC` |

DevSendPDU's fifth stack argument is a length; DevRecvPDU's is an in/out length
pointer. The decompiled receive prototype omitted two parameters; the bridge
uses the verified five-argument instruction-level ABI. DevGetBearerState uses
r2 for availability and r3 for connected state. NetDataArrived takes the bearer
and the original connection/local-port cookie, **not** the packet length. XEDK1
uses local port 8502 (`2136`); preserving this cookie is required for WDP demux.
The native browser event loop (`01222BB4`) provides an additional safe callback
injection point.

Injected calls use a dedicated 16-KiB stack allocated by the guest once per boot.
Borrowing the idle CPU stack overwrote parked task frames during deep WAP receive
callbacks, freezing the phone when previewing downloaded MMFs. IRQ delivery is
deferred during these short native transactions; device time and interrupt latches
still advance, and a pending unmasked interrupt is serviced before a new call.
The original CPU context is restored on return.

The native peer container is 30 bytes: an eight-byte meaningful IPv4 header/address
plus padding. DevRecvPDU copies all 30 bytes, matching firmware at `0120F41A`.
Send normalizes padding to zero; gateway endpoint matching compares only the
meaningful eight bytes rather than uninitialized padding.

See [downloaded-MMF preview regression](wap-preview-fix.md).

## Verification

Release tests cover SIM persistence and rejection, the two-stage incoming call,
CPU-state preservation around native injection, WTP caching/SAR/retries/malformed
input, and native transport send/receive against the offline gateway.

Local XEDK1 firmware/GUI probes also verified:

- Native WAP browser connects, exchanges WTP acknowledgements and renders the
  built-in gateway landing page from a WBXML response.
- Incoming call ringing, native Answer, connected timer, and End.
- Outgoing phone dial 12, host Accept window, connected phone UI, and End.
- Incoming SMS notification, inbox and UCS-2 message body.
- Native SMS send to 123, Message sent confirmation and SIM stored sent record.
- Two-picture bulk GUI import, both queue entries Imported, both visible in the
  phone directory, followed by automatic NVM save.

Probes use scratch copies or read-only phone NVM. No ROMs, proprietary audio
libraries, user content or snapshots are distributed.
