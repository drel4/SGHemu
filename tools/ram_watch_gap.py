"""Show the writes immediately before and after the largest silence in a watch log.

The aggregate report and even the per-window split hide the transition. This prints
the lead-in to the biggest inter-write gap, so the last few events before the
generator goes dry are readable in order.

usage: python tools\\ram_watch_gap.py [LOG] [before] [after]
"""
import sys

sys.path.insert(0, "tools")
import ram_watch_report as report


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/gui-ram-watch.log"
    before = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    after = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    symbols = report.load_symbols(report.DEFAULT_LST)
    addresses = [s[0] for s in symbols]
    writes = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        match = report.WRITE.match(line)
        if match:
            writes.append((int(match.group(1)), int(match.group(2), 16),
                           int(match.group(4), 16), int(match.group(5), 16)))
    best = max(range(1, len(writes)), key=lambda i: writes[i][0] - writes[i - 1][0])
    print("%s: %d writes" % (path, len(writes)))
    print("largest gap: %d ticks, %d -> %d" % (
        writes[best][0] - writes[best - 1][0], writes[best - 1][0], writes[best][0]))
    print("\n  %-12s %-8s %-28s %s" % ("tick", "addr", "writer", "value"))
    for i in range(max(0, best - before), min(len(writes), best + after)):
        tick, pc, address, value = writes[i]
        who = report.resolve(symbols, addresses, pc) or ("PC=%08x" % pc)
        mark = "  <-- silence" if i == best else ""
        print("  %-12d %08x %-28s %08x%s" % (tick, address, who[:28], value, mark))
    return 0


if __name__ == "__main__":
    sys.exit(main())
