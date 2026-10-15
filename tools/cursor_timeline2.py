"""Extract the true live PCM-cursor reads from a ma3.c replay trace.

MaDevDrv_ReceiveData(0x151-ch, 0) is a status-3 parameter read of address
0x150/0x151 with read buffer 1. In a trace that shows as the four port-4 data
bytes 50|51, 82, 81, 80 while the selector is 03, followed by the read-buffer
select 11 and the data read.

Usage: python tools\\cursor_timeline2.py <out.log> [lo] [hi]
"""
import sys


def main():
    path = sys.argv[1]
    sel = -1
    hist = []          # pending port-4 bytes while a status-3 packet is open
    reads = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) < 5 or p[2] not in ("R", "W"):
            continue
        ts, port, val = int(p[0]), p[3], int(p[4], 16)
        if p[2] == "W" and port == "0":
            sel = val
            hist = []
        elif p[2] == "W" and port == "4":
            if (sel & 0x3F) == 3:
                hist.append(val)
                if len(hist) >= 2 and hist[0] in (0x50, 0x51) and hist[1] == 0x82:
                    reads.append((ts, hist[0]))
        elif p[2] == "R" and port == "4" and (sel & 0x0F) == 1:
            if reads and reads[-1][0] <= ts:
                pass
    print("cursor-read packets: %d" % len(reads))
    if len(sys.argv) > 2:
        lo = int(sys.argv[2])
        hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 62
        for ts, addr in reads:
            if lo <= ts <= hi:
                print("   %d ch=%d" % (ts, addr - 0x50))
        return
    ch = {}
    for ts, addr in reads:
        ch.setdefault(addr - 0x50, []).append(ts)
    for c in sorted(ch):
        times = ch[c]
        print("channel %d: %d reads, first %d last %d" % (c, len(times), times[0], times[-1]))
        gaps = sorted(((times[i] - times[i - 1], times[i]) for i in range(1, len(times))), reverse=True)
        for gap, ts in gaps[:6]:
            print("   gap %.2fs before ts %d" % (gap / 2166667.0, ts))


if __name__ == "__main__":
    main()
