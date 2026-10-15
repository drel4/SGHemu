"""Attribute the RAM writes captured by MA3_ACCESS_WATCH to firmware functions.

MA3_ACCESS_WATCH=<start>,<end> (src/gui.c) arms the bus watch over an arbitrary
guest range, writes only, recording the guest PC:

    <tick> PC=<pc> W<width> <addr> value=<value> RAM

This resolves each PC against the firmware linker listing so a changed RAM field
is traced to the exact function and offset, instead of being guessed from the
decompiler -- i.e. it answers "which routine writes this sound-driver state byte,
in what order, and to what values".

Usage: python tools\\ram_watch_report.py [build/gui-ram-watch.log] [lst] [top]
       lst defaults to models\\c100\\roms\\C100XEDK1\\C100XEDK1.lst
"""
import bisect
import re
import sys

WRITE = re.compile(r"^(\d+) PC=(\w+) W(\d+) (\w+) value=(\w+)")
DEFAULT_LST = "models\\c100\\roms\\C100XEDK1\\C100XEDK1.lst"


def load_symbols(path):
    """(address, size, name) for every Code symbol in the linker listing.

    Lines look like `MaDevDrv_Fifo  0x011c754e  Thumb Code  620  madevdrv.o(.text)`,
    so locate the `0x` token rather than relying on fixed columns."""
    symbols = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        parts = line.split()
        if "Code" not in parts:
            continue
        try:
            at = next(i for i, token in enumerate(parts) if token.startswith("0x"))
            address = int(parts[at], 16)
            size = int(parts[parts.index("Code") + 1])
            name = parts[at - 1]
        except (ValueError, IndexError, StopIteration):
            continue
        if size > 0 and at >= 1:
            symbols.append((address, size, name))
    symbols.sort()
    return symbols


def resolve(symbols, addresses, pc):
    index = bisect.bisect_right(addresses, pc) - 1
    if index < 0:
        return None
    address, size, name = symbols[index]
    if pc >= address + size:
        return None
    return "%s+0x%x" % (name, pc - address)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/gui-ram-watch.log"
    listing = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_LST
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    symbols = load_symbols(listing)
    addresses = [s[0] for s in symbols]
    print("%s: %d code symbols from %s" % (listing, len(symbols), listing))
    writes = []
    try:
        for line in open(path, "r", encoding="utf-8", errors="replace"):
            match = WRITE.match(line)
            if match:
                writes.append((int(match.group(1)), int(match.group(2), 16),
                               int(match.group(4), 16), int(match.group(5), 16)))
    except OSError as error:
        print("cannot read %s: %s" % (path, error))
        return 1
    print("%s: %d writes" % (path, len(writes)))
    if not writes:
        print("no writes captured. Set MA3_ACCESS_WATCH before the tone is played,")
        print("and make sure the range covers the RAM you care about.")
        return 0
    # Aggregate by (address, writer) so the order of state changes is readable.
    sites = {}
    for tick, pc, address, value in writes:
        who = resolve(symbols, addresses, pc) or ("PC=%08x" % pc)
        key = (address, who)
        site = sites.setdefault(key, {"count": 0, "first": tick, "last": tick,
                                      "values": []})
        site["count"] += 1
        site["last"] = tick
        if len(site["values"]) < 8 and (not site["values"] or site["values"][-1] != value):
            site["values"].append(value)
    print("\naddr        writer                          writes      first      last  values")
    for (address, who), site in sorted(sites.items(),
                                       key=lambda item: (-item[1]["count"], item[0][0]))[:top]:
        print("  %08x  %-30s %6d %10d %9d  %s" % (
            address, who[:30], site["count"], site["first"], site["last"],
            " ".join("%08x" % v for v in site["values"])))
    # Timeline: a silence shows up as an empty bin and as a large inter-write gap,
    # so the last write before each gap names the routine that stopped feeding.
    first, last = writes[0][0], writes[-1][0]
    span = max(1, last - first)
    bins = 48
    counts = [0] * bins
    for tick, _, _, _ in writes:
        counts[min(bins - 1, (tick - first) * bins // span)] += 1
    peak = max(counts) or 1
    print("\ntimeline over ticks %d..%d (%d bins, %d writes):" % (first, last, bins, len(writes)))
    for i, count in enumerate(counts):
        print("  %12d  %-38s %d" % (first + span * i // bins, "#" * (count * 38 // peak), count))
    gaps = sorted(((writes[i][0] - writes[i - 1][0], i) for i in range(1, len(writes))),
                  reverse=True)
    print("\nlargest gaps between consecutive writes (silence candidates):")
    for gap, i in gaps[:12]:
        before, after = writes[i - 1], writes[i]
        who_b = resolve(symbols, addresses, before[1]) or ("PC=%08x" % before[1])
        who_a = resolve(symbols, addresses, after[1]) or ("PC=%08x" % after[1])
        print("  gap %10d  %08x %-26s v=%08x  ->  %08x %-26s v=%08x" % (
            gap, before[2], who_b[:26], before[3], after[2], who_a[:26], after[3]))
    distinct = len({address for address, _ in sites})
    print("\ndistinct addresses written: %d of %d total writes" % (distinct, len(writes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
