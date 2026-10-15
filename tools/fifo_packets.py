"""Segment a WST/WDT capture into hardware packets and histogram them.

Usage: python tools\\fifo_packets.py <log> [status]

Both the capture proxy and the emulator's MA5HW_TRACE hook log every
Hw_WriteStatusFlagReg / Hw_WriteDataReg call. A status write opens a packet;
the data bytes that follow belong to it. Grouping by the status value (the low
six bits select the command) shows which payloads each side repeats, so the
MA-3 firmware's periodic stream packets can be matched against SM5W's player.
"""
import sys
from collections import Counter


def packets(path, want=None):
    out = []
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("WST "):
            if cur:
                out.append(cur)
            cur = {"status": int(line[4:], 16) & 0x3F, "data": []}
        elif line.startswith("WDT ") and cur is not None:
            cur["data"].append(int(line[4:], 16))
    if cur:
        out.append(cur)
    return [p for p in out if want is None or p["status"] == want]


def main():
    path = sys.argv[1]
    want = int(sys.argv[2], 16) if len(sys.argv) > 2 else 1
    pk = packets(path, want)
    print("== %s status=%d : %d packets" % (path, want, len(pk)))
    sizes = Counter(len(p["data"]) for p in pk)
    print("   payload sizes: %s" % dict(sorted(sizes.items())))
    shapes = Counter(" ".join("%02x" % b for b in p["data"][:8]) +
                     (" ..." if len(p["data"]) > 8 else "") for p in pk)
    print("   most common shapes:")
    for shape, n in shapes.most_common(18):
        print("      %5d x  %s" % (n, shape))
    # Recurring packets whose first byte is 0x80 (zero delta) and length <= 5:
    # those are the timed single-register events in the MA-3 stream.
    small = Counter()
    for p in pk:
        d = p["data"]
        if len(d) <= 5:
            small[" ".join("%02x" % b for b in d)] += 1
    print("   small (<=5 byte) packets:")
    for shape, n in small.most_common(20):
        print("      %5d x  %s" % (n, shape))


if __name__ == "__main__":
    main()
