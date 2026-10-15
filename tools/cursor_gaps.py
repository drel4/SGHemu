"""Correlate the guest's live PCM-cursor readbacks with refill gaps.

Usage: python tools\\cursor_gaps.py <out.log> [gap_ticks]

Prints every cursor read in order, flagging reads that are followed by a gap
longer than gap_ticks (default 0.25 s at 2166667 ticks/sec). The MA-3 driver
only refills the streamed ring while the channel's stream state is 2/3, and it
re-reads the live cursor on every refill, so a gap in these reads means the
driver left the streaming state.
"""
import sys


def main():
    path = sys.argv[1]
    thresh = int(sys.argv[2]) if len(sys.argv) > 2 else 2166667 // 4
    sel = -1
    reads = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) < 5 or p[2] not in ("R", "W"):
            continue
        if p[2] == "W" and p[3] == "0":
            sel = int(p[4], 16)
        elif p[2] == "R" and p[3] == "4" and sel == 0x11:
            reads.append((int(p[0]), int(p[4], 16)))
    print("cursor reads: %d" % len(reads))
    for i, (ts, val) in enumerate(reads):
        gap = reads[i + 1][0] - ts if i + 1 < len(reads) else 0
        flag = ""
        if gap > thresh:
            flag = "   <-- gap %.2fs" % (gap / 2166667.0)
        print("  %3d  %10d  0x%02x%s" % (i, ts, val, flag))


if __name__ == "__main__":
    main()
