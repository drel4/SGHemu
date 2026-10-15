"""Per-window write attribution for the MA3_ACCESS_WATCH log.

The whole-capture table in ram_watch_report.py hides phase changes: a silence and
a burst look the same once aggregated. This splits the capture into tick windows
and shows, for each, which firmware routines are doing the writing, so a plateau
or a silence can be attributed to the routine that stopped.

usage: python tools\\ram_watch_windows.py LOG start1,end1 [start2,end2 ...]
       ticks are the log's own tick column (as printed by ram_watch_report.py)
"""
import collections
import sys

sys.path.insert(0, "tools")
import ram_watch_report as report


def load(path):
    writes = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        match = report.WRITE.match(line)
        if match:
            writes.append((int(match.group(1)), int(match.group(2), 16),
                           int(match.group(4), 16), int(match.group(5), 16)))
    return writes


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path = sys.argv[1]
    windows = []
    for spec in sys.argv[2:]:
        start, end = (int(part, 0) for part in spec.split(","))
        windows.append((start, end))
    symbols = report.load_symbols(report.DEFAULT_LST)
    addresses = [s[0] for s in symbols]
    writes = load(path)
    print("%s: %d writes" % (path, len(writes)))
    for start, end in windows:
        inside = [w for w in writes if start <= w[0] < end]
        by_writer = collections.Counter()
        by_address = collections.Counter()
        for _, pc, address, _ in inside:
            by_writer[report.resolve(symbols, addresses, pc) or ("PC=%08x" % pc)] += 1
            by_address[address] += 1
        print("\n=== ticks %d..%d: %d writes ===" % (start, end, len(inside)))
        print("  top writers:")
        for who, count in by_writer.most_common(14):
            print("    %8d  %s" % (count, who))
        print("  top addresses:")
        for address, count in by_address.most_common(10):
            print("    %8d  %08x" % (count, address))
    return 0


if __name__ == "__main__":
    sys.exit(main())
