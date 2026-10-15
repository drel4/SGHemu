"""Extract the guest's live PCM-cursor reads from a ma3.c replay trace.

The firmware reads the cursor with MaDevDrv_ReceiveData(0x151-ch, 0): a status-3
parameter read whose acknowledgement status is (1<<(sel+4))|(sel+1) = 0x11.
So a port-4 read immediately after a selector write of 0x11 carries the cursor
low bits.

Usage: python tools\\cursor_timeline.py out.log [lo_ts] [hi_ts]

Without a window the script reports a summary plus the longest gaps between
cursor reads; with a window it lists every read in that tick range.
"""
import sys


def main():
    path = sys.argv[1]
    sel = -1
    cursor = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        parts = line.split()
        if len(parts) < 6 or parts[2] not in ("R", "W"):
            continue
        if parts[2] == "W" and parts[3] == "0":
            sel = int(parts[4], 16)
        elif parts[2] == "R" and parts[3] == "4" and sel == 0x11:
            cursor.append((int(parts[0]), int(parts[4], 16)))
    print("cursor reads: %d" % len(cursor))
    if not cursor:
        return
    if len(sys.argv) > 2:
        lo = int(sys.argv[2])
        hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 62
        shown = [(t, v) for t, v in cursor if lo <= t <= hi]
        print("reads in [%d,%d]: %d" % (lo, hi, len(shown)))
        for t, v in shown:
            print("   %d -> 0x%02x" % (t, v))
        return
    changes = sum(1 for i in range(1, len(cursor)) if cursor[i][1] != cursor[i - 1][1])
    print("distinct values: %d  changes: %d" % (len(set(v for _, v in cursor)), changes))
    gaps = sorted(((cursor[i][0] - cursor[i - 1][0], i) for i in range(1, len(cursor))),
                  reverse=True)
    print("longest gaps between cursor reads:")
    for gap, i in gaps[:12]:
        print("   %d ticks (%.2fs) before ts %d -> 0x%02x"
              % (gap, gap / 2166667.0, cursor[i][0], cursor[i][1]))
    print("first 12:")
    for t, v in cursor[:12]:
        print("   %d -> 0x%02x" % (t, v))
    print("last 12:")
    for t, v in cursor[-12:]:
        print("   %d -> 0x%02x" % (t, v))


if __name__ == "__main__":
    main()
