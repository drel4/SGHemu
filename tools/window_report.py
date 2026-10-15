"""Characterise a ma3.c replay trace between two tick stamps.

Usage: python tools\\window_report.py <out.log> <lo> <hi>

Reports the access mix (read/write port, selector), the firmware-visible
addresses the guest touches, and how often the native status byte said the
delayed FIFO was empty.
"""
import sys
from collections import Counter


def main():
    path, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    sel = -1
    ops = Counter()
    addrs = Counter()
    status = Counter()
    n = 0
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) < 5 or p[2] not in ("R", "W"):
            continue
        ts = int(p[0])
        if not (lo <= ts <= hi):
            continue
        n += 1
        port, val = p[3], int(p[4], 16)
        ops["%s%s" % (p[2], port)] += 1
        if p[2] == "W" and port == "0":
            sel = val
            ops["status %02x" % (val & 0x3f)] += 1
        elif p[2] == "R" and port == "0":
            status[val] += 1
        elif p[2] == "W" and port == "4":
            if (sel & 0x3f) == 3:
                addrs[val] += 1
    print("accesses in [%d,%d]: %d" % (lo, hi, n))
    for k, v in ops.most_common(20):
        print("   %-12s %d" % (k, v))
    print("   native status reads:")
    for k, v in sorted(status.items()):
        print("      %02x : %d" % (k, v))
    print("   status-3 data bytes (address stream / read params), top 24:")
    for k, v in addrs.most_common(24):
        print("      %02x : %d" % (k, v))


if __name__ == "__main__":
    main()
