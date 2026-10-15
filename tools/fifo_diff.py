#!/usr/bin/env python3
"""Structural diff of two MA-5 FIFO feeds: the SMW5 reference vs the emulator.

A feed is a sequence of ops.  `WST <sel>` selects a destination; the `WDT
<byte>` lines that follow are the data written to it.  A `WST` with no
following `WDT` is a select performed only to *read* the destination (both the
capture proxy and src/ma5_hw.c log writes only).

Comparing two feeds per select byte -- op count, total payload bytes, and a
sha1 of the concatenated payload -- separates two very different situations:

    same payload sha1   -> the same bytes were delivered, only chunked
                           differently (harmless to the synth);
    different sha1      -> genuinely different content on that register.

usage: python tools\\fifo_diff.py REFERENCE.log EMULATOR.log
env:   FIFO_DIFF_OUT=<path>   also write the report as UTF-8 to <path>
"""
import collections
import hashlib
import os
import sys


def ops(path):
    out = []
    for line in open(path, errors="ignore"):
        line = line.strip()
        if line.startswith("WST "):
            out.append([int(line[4:6], 16), []])
        elif line.startswith("WDT ") and out:
            out[-1][1].append(int(line[4:6], 16))
    return out


def payloads(stream):
    acc = collections.defaultdict(bytearray)
    for sel, data in stream:
        acc[sel] += bytes(data)
    return acc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out = os.environ.get("FIFO_DIFF_OUT")
    if out:
        sys.stdout = open(out, "w", encoding="utf-8")

    ref, emu = ops(sys.argv[1]), ops(sys.argv[2])
    rp, ep = payloads(ref), payloads(emu)
    rn = collections.Counter(s for s, _ in ref)
    en = collections.Counter(s for s, _ in emu)

    print("reference %s: %d ops, %d payload bytes" % (sys.argv[1], len(ref), sum(len(d) for _, d in ref)))
    print("emulator  %s: %d ops, %d payload bytes" % (sys.argv[2], len(emu), sum(len(d) for _, d in emu)))

    print("\n%-4s %8s %8s %9s %9s  %s" % ("sel", "refops", "emuops", "refbytes", "emubytes", "payload"))
    for sel in sorted(set(rn) | set(en)):
        rb, eb = rp.get(sel, b""), ep.get(sel, b"")
        if rb and not eb:
            verdict = "<== emulator never writes payload"
        elif eb and not rb:
            verdict = "<== reference never writes payload"
        elif rb and rb == eb:
            verdict = "same"
        elif rb and eb:
            verdict = "DIFFERENT"
        else:
            verdict = "(reads only)"
        print("%02x   %8d %8d %9d %9d  %s" % (sel, rn[sel], en.get(sel, 0), len(rb), len(eb), verdict))

    onlyr = sorted(set(rn) - set(en))
    onlye = sorted(set(en) - set(rn))
    print("\nselect bytes only in reference: %s" % (" ".join("%02x" % x for x in onlyr) or "-"))
    print("select bytes only in emulator:  %s" % (" ".join("%02x" % x for x in onlye) or "-"))

    n = min(len(ref), len(emu))
    for i in range(min(len(ref), len(emu))):
        if ref[i] != emu[i]:
            n = i
            break
    print("\nfirst divergence in op order at op %d:" % n)
    for label, stream in (("ref", ref), ("emu", emu)):
        for i in range(max(0, n - 5), min(len(stream), n + 6)):
            print("    %s%s %4d  sel %02x  %s" % ("->" if i == n else "  ", label, i, stream[i][0],
                                                  " ".join("%02x" % b for b in stream[i][1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
