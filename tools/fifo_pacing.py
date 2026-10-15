"""FIFO feed pacing from a WST/WDT/GEN capture.

Usage: python tools\\fifo_pacing.py <log> [<log2> ...]

Both the M5_EmuHw capture proxy and the emulator's MA5HW_TRACE hook emit
WST/WDT lines, and both emit a GEN marker every 64th Hw_Generate call
(printing that call's frame count). Treating each GEN interval as 64 equal
calls gives a time base, so we can see how many FIFO payload bytes the player
queues per interval and where it lets the queue run dry.
"""
import sys


def scan(path):
    intervals = []
    wdt = 0
    delayed = 0
    in_delayed = False
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("GEN "):
            if cur is not None:
                intervals.append(cur)
            cur = {"count": int(line[4:]), "wdt": wdt, "delayed": delayed,
                   "calls": 0}
            wdt = delayed = 0
        elif line.startswith("WST "):
            v = int(line[4:], 16)
            in_delayed = (v & 0x3f) == 1
        elif line.startswith("WDT "):
            wdt += 1
            if in_delayed:
                delayed += 1
        if cur is not None:
            cur["calls"] += 1
    if cur is not None:
        intervals.append(cur)
    return intervals


def report(path):
    iv = scan(path)
    print("== %s : %d GEN intervals" % (path, len(iv)))
    t = 0.0
    idle = []
    total_wdt = total_del = 0
    for i, x in enumerate(iv):
        frames = 64 * x["count"]
        t += frames / 48000.0
        total_wdt += x["wdt"]
        total_del += x["delayed"]
        if x["wdt"] == 0:
            idle.append((i, t))
    print("   total WDT=%d delayed=%d -> %.0f bytes/s (delayed %.0f/s)"
          % (total_wdt, total_del, total_wdt / t if t else 0,
             total_del / t if t else 0))
    print("   idle GEN intervals (no FIFO bytes): %d" % len(idle))
    for i, ts in idle[:40]:
        print("      #%d  t=%.2fs" % (i, ts))
    if len(iv) > 1:
        lo = min(x["wdt"] for x in iv)
        hi = max(x["wdt"] for x in iv)
        print("   WDT per interval min=%d max=%d" % (lo, hi))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        report(p)
