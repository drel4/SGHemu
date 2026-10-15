# WAP URL lifetime and reopening

Two separate virtual-network bugs explained the first-open landing page and
second-open failure on the additional C100 firmwares.

## Deferred URL ownership

XEDD1's native Goto URL callback queued event `0B` with the correct edit-buffer
pointer. The browser deferred that event while starting up; the scene then
cleared the edit buffer in `kit_CmdReq`. The event's second dispatch saw an empty
string and emitted WSP `40 00` (GET with zero-length URL), which legitimately
selected the gateway's blank-home landing page. The typed address was not lost
by HTML conversion or HTTP fetching.

The bridge now retains nonempty URL-event arguments in guest-owned memory and
still executes the firmware's `EventPostEvent` function. Its existing per-boot
native allocation contains a separate 4-KiB pool (four 1024-byte URL slots), in
addition to the unchanged 16-KiB stack. URLs already pointing into the retained
pool are not recopied when deferred. Invalid/unterminated pointers are not
rewritten. No firmware bytes, NVM settings, menu result or browser URL are
invented; the copied text is exactly what the native scene queued.

## WSP session Resume

The gateway answered Resume (`09`) with ConnectReply (`02`). UP.Browser rejected
it with WTP Abort reason `E0`. Resume must receive a normal Reply (`04`), as
specified in [WAP-230-WSP sections 8.2.5.2–8.2.5.3](https://www.openmobilealliance.org/tech/affiliates/wap/wap-230-wsp-20010705-a.pdf).
The response is now an empty successful Reply. Session-ID and reserved
capability-length validation reject invalid resumes without starting an HTTP
fetch. This also matches the supplied WAPWarp responder's Resume handling.

## Verification

Synthetic tests cover retained text after clearing the source edit buffer,
repeat/deferred retention, invalid pointers, Connect → Resume → GET, invalid
session IDs and invalid resume capabilities. Existing WTP retransmission,
transaction-cache, SAR and pagination tests remain enabled.

A read-only XEDD1 handset run typed the external directory URL through the
keypad. The gateway received that full URL and returned its directory listing,
not the landing page. After exiting and reopening the browser, the handset
acknowledged the Resume Reply and issued a second full-URL GET successfully.
Firmware, captures and NVM snapshots remain private test inputs/outputs.

The updated 40-pin driver-selection profiles were also checked: XEDK1 loaded
the offline page, exited, resumed the session and fetched the external directory
as a distinct second URL (full GET, 319-byte response and native rendered list).
XEDA2 rendered that same external directory after configuring its fresh profile
through the keypad. Its initial blank IP / Secure profile correctly produced
"Internet service is not enabled"; use gateway `10.0.0.1`, Non-secure, port 9201.
These tests do not rewrite users' saved WAP profiles or input snapshots.
