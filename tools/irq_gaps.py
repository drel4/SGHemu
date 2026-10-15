"""Histogram the accepted MA-3 IRQ intervals in a ma3.c replay trace.

The trace emits one `-------- IRQ` marker per interrupt the guest accepts. The
firmware's refill pump (MaDevDrv_Timer0 -> MaDevDrv_StreamHandler) runs from the
MA-3 Timer A interrupt, documented at ~20 ms, so the interval between these
markers shows whether the emulated timer cadence matches.

Usage: python tools\\irq_gaps.py <out.log> [ticks_per_sec]
"""
import sys
from collections import Counter


def main():
    path = sys.argv[1]
    hz = int(sys.argv[2]) if len(sys.argv) > 2 else 2166667
    times = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        if "IRQ" in line:
            parts = line.split()
            if parts and parts[1] == "--------" and parts[2] == "IRQ":
                times.append(int(parts[0]))
    print("accepted IRQs: %d" % len(times))
    if len(times) < 2:
        return
    gaps = [times[i] - times[i - 1] for i in range(1, len(times))]
    buckets = Counter()
    for g in gaps:
        buckets[round(g / hz * 1000)] += 1
    print("interval histogram (ms : count), interval <= 60 ms:")
    for ms in sorted(k for k in buckets if k <= 60):
        print("   %4d ms : %d" % (ms, buckets[ms]))
    other = sum(v for k, v in buckets.items() if k > 60)
    if other:
        print("   >60 ms : %d" % other)
    print("first 12 intervals (ms): %s" % ", ".join(
        "%.2f" % (g / hz * 1000) for g in gaps[:12]))


if __name__ == "__main__":
    main()
