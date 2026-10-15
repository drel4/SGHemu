"""Decode MA-3 status-3 parameter reads from a ma3.c replay trace.

A status-3 packet is: address bytes terminated by bit 7, then the read-buffer
selector byte (bit 7 set, low bits select RBO). ma3.c then answers the read
from parameter RAM, native memory, or the live PCM cursor depending on the
address. This tool reconstructs (tick, address, rbo, value) tuples so the
guest's polling pattern is visible.

Usage: python tools\\param_reads.py <out.log> [lo] [hi] [top]
"""
import sys
from collections import Counter


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 62
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 30
    sel = -1
    addr = 0
    nbytes = 0
    rbo = 0
    reads = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) < 5 or p[2] not in ("R", "W"):
            continue
        ts, port, val = int(p[0]), p[3], int(p[4], 16)
        if p[2] == "W" and port == "0":
            sel = val & 0x3F
            addr = nbytes = 0
        elif p[2] == "W" and port == "4" and sel == 3:
            if nbytes < 3:
                addr |= (val & 0x7F) << (7 * nbytes)
            if val & 0x80:
                nbytes = 4 if nbytes >= 3 else nbytes + 1
                rbo = val & 0x7F if False else 0
        elif p[2] == "R" and port == "4" and (sel & 0x0F) in (1, 2, 3):
            reads.append((ts, addr, sel & 0x0F, val))
    window = [r for r in reads if lo <= r[0] <= hi]
    print("parameter reads: %d total, %d in [%d,%d]" % (len(reads), len(window), lo, hi))
    c = Counter(r[1] for r in window)
    print("most-polled addresses:")
    for a, n in c.most_common(top):
        vals = Counter(r[3] for r in window if r[1] == a)
        print("   0x%03x : %5d   values %s" % (a, n, dict(vals.most_common(4))))


if __name__ == "__main__":
    main()
