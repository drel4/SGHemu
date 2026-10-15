# Native Java DNS/TCP networking

Enable Cellular and select either **Internet** or **Internet + built-in WAP
gateway**. The latter still needs the usual non-secure WAP profile (gateway
10.0.0.1, port 9201) to browse to a JAD. Java then downloads its JAR using its
own TCP connection, not the browser's WSP/UDP channel.

The shared `java_net.c` bridge implements the firmware's DS socket boundary.
Jbed, HTTP parsing, Java exceptions, JAD validation, installation and storage all
remain native firmware code. The `java_net=` model directive contains these
even Thumb entry PCs, in order:

`Netopen, Netclose, DSSocket, DSConnect, DSClose, DSWrite, DSRead,
DSGetRDataLen, DSGetHostByName, OnAsyncNetworkEvent, DSGetLocalIP,
DSGetRemoteIP, DSGetLocalPort, DSGetRemotePort, DSBind`.

Bindings are separately recovered for XEDK1, XEDA2 and XEDD1, not guessed with
one firmware-wide offset. Config loading rejects missing, duplicate, odd and
unmapped entries, and requires the native cellular scheduler bindings.

## Scheduling and ownership

- Eight nonblocking IPv4 TCP sockets, with distinct signed-16-bit DS handles.
- DNS runs on a resolver worker. The worker owns copies of its input/output;
  it never accesses guest RAM or the machine. Reset drops its reference without
  waiting for a potentially slow host DNS lookup.
- DNS and socket completion queue native `OnAsyncNetworkEvent` calls at the
  existing scheduler/event-loop safe points. Event `0x220` completes DNS,
  `0x222` completes connect, `0x1fa` resumes a read, and `0x223` reports failure.
  Event `0x221` does not complete a connect in this firmware.
- Initial DNS deliberately returns pending, allowing Jbed to install its signal
  and result pointers before the completion callback polls the result.
- Writes copy accepted bytes into a bounded host-owned queue, so reuse of the
  Java buffer cannot change an in-flight request. Reads distinguish would-block
  from EOF. No synthetic download success or suppressed exceptions.
- Host socket polling occurs only at native safe points, not every instruction.
  All socket calls are nonblocking; only the DNS worker can wait.

Both packet-data modes enable this bridge; Off or disabled Cellular rejects
new network operations. Reset, mode changes and machine destruction close host
sockets. An outstanding native operation can still need its firmware timeout
after disabling the network. Java UDP/listening sockets, reverse DNS and TLS
offload are not implemented. Host/server failures remain real failures.

Diagnostics shows Java DNS/connect counts, byte totals and the last DNS error.
With File logging enabled, DNS requests/results also appear in the unmapped log.

## Verification

The Release test uses system DNS plus a loopback server: asynchronous completion,
bind/connect, buffered-write ownership, read notifications, available bytes, EOF,
invalid handles/pointers, failed DNS and reset with a resolver in flight.
Machine tests cover config validation and the execution hook.

A read-only XEDK1 NVM probe opened a JAD through the native WAP browser, accepted
the install prompt, fetched the original 63,614-byte Bobby Carrot JAR over TCP,
and displayed the firmware's installation-success screen. A second run resolved
`localtest.me` asynchronously to a private loopback HTTP fixture and downloaded
the same JAR. The fixture uses a user-supplied game; no game, ROM, NVM or private
capture is distributed. This does not imply compatibility with arbitrary MIDlets
or modern HTTPS servers.
