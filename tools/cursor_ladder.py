"""Analyse the MA3_CURSOR_TRACE readback lines produced by ma3replay.

Each line is:
    <ticks> -------- CURSOR ch=<c> fmt=<f> raw=<r> byte=<b> biased=<j> val=<v>

The firmware (MaDevDrv_StreamUpdate) turns the answer into its ring target as

    uVar6  = (val & 0x3e) << 4          fmt <  2   (32-byte ladder, 0..0x3e0)
    uVar6  = (val & 0x1f) << 5          fmt >= 2
    target = uVar6 == 0 ? 0x3c0 : uVar6 - 0x20

and ends the segment when uVar6 is cyclically between +0x260 and +0x264. This
script reconstructs uVar6/target and reports the ladder the guest actually saw,
so a wrong bias, unit or wrap mask shows up as a discontinuity or a missing
step in the wrap region.

Usage: python tools\\cursor_ladder.py <ctrace.log>
"""
import sys


def main():
    path = sys.argv[1]
    rows = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        if "CURSOR" not in line:
            continue
        parts = line.split()
        fields = {}
        for p in parts[3:]:
            if "=" in p:
                k, v = p.split("=", 1)
                fields[k] = int(v, 16) if k == "val" else int(v, 0)
        rows.append((int(parts[0]), fields))
    print("readbacks: %d" % len(rows))
    if not rows:
        return
    prev_u = None
    prev_t = None
    gaps = []
    print("%12s %3s %5s %6s %6s %6s %5s %6s %8s" %
          ("ticks", "ch", "fmt", "raw", "byte", "biased", "uVar6", "target", "step"))
    for ticks, f in rows:
        val = f.get("val", 0)
        fmt = f.get("fmt", 0)
        if fmt < 2:
            u = (val & 0x3E) << 4
        else:
            u = (val & 0x1F) << 5
        target = 0x3C0 if u == 0 else (u - 0x20) & 0xFFFF
        step = "" if prev_u is None else "%d" % ((u - prev_u) & 0xFFFF)
        print("%12d %3d %5d %6d %6d %6d %5d %6d %8s" %
              (ticks, f.get("ch", -1), fmt, f.get("raw", -1), f.get("byte", -1),
               f.get("biased", -1), u, target, step))
        if prev_t is not None:
            gaps.append((ticks - prev_t, ticks))
        prev_u, prev_t = u, ticks
    if gaps:
        big = [(g, t) for g, t in gaps if g > 2_000_000]
        print("\ngaps over 2,000,000 ticks (%.2f s at 2.166667 MHz): %d" %
              (2_000_000 / 2166667.0, len(big)))
        for g, t in big:
            print("   %12d ticks (%.2f s) before ts %d" % (g, g / 2166667.0, t))


if __name__ == "__main__":
    main()
