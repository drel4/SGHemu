#!/usr/bin/env python3
"""Find the time range in the firmware trace that carries a reference MMF's
sample data, then slice the trace to that range.

Usage:
  extract_ringtone.py <guest-trace> <ref.log> <out-trace> [pad-ticks]
"""
import re
import sys

GUEST = re.compile(r'^\s*(\d+)\s+([0-9a-fA-F]+)\s+([WR])\s+(\d+)\s+([0-9a-fA-F]{2})')
WIN = 96


def ref_windows(path):
    g = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        if line.startswith('WST '):
            g.append([int(line[4:6], 16), []])
        elif line.startswith('WDT ') and g:
            g[-1][1].append(int(line[4:6], 16))
    out = set()
    for sel, data in g:
        if len(data) >= WIN:
            b = bytes(data)
            for i in range(len(b) - WIN):
                w = b[i:i + WIN]
                if len(set(w)) >= 14:
                    out.add(w)
    return out


def main():
    trace, ref, out = sys.argv[1], sys.argv[2], sys.argv[3]
    pad = int(sys.argv[4]) if len(sys.argv) > 4 else 3000000
    windows = ref_windows(ref)
    print(f"reference {ref}: {len(windows)} distinctive {WIN}-byte windows")

    lines, ops = [], []
    for line in open(trace, errors='ignore'):
        m = GUEST.match(line)
        if m:
            ts, pc, rw, port, val = m.groups()
            ops.append((int(ts), rw, int(port), int(val, 16)))
            lines.append(line.rstrip('\n'))

    g, matches = [], []
    for ts, rw, port, val in ops:
        if rw == 'W' and port == 0:
            g.append([val, [], ts])
        elif rw == 'W' and port == 4 and g:
            g[-1][1].append(val)
    for sel, data, ts in g:
        if len(data) >= WIN:
            b = bytes(data)
            for i in range(len(b) - WIN):
                if b[i:i + WIN] in windows:
                    matches.append(ts)
                    break
    print(f"ops: {len(g)}, matching ops: {len(matches)}")
    if not matches:
        return
    lo, hi = min(matches), max(matches)
    print(f"match ts range: {lo} .. {hi}  span={hi-lo}")
    buckets = [0] * 16
    for ts in matches:
        buckets[min(15, (ts - lo) * 16 // max(1, hi - lo))] += 1
    print("distribution:", buckets)

    lo2, hi2 = lo - pad, hi + pad
    n = 0
    with open(out, 'w') as f:
        f.write("3723 -------- AUDIO silent at frame 96\n")
        f.write(f"# slice of {trace} covering {ref} sample writes [{lo2},{hi2}]\n")
        for line, (ts, rw, port, val) in zip(lines, ops):
            if lo2 <= ts <= hi2:
                f.write(line + "\n")
                n += 1
    print(f"wrote {n} lines to {out} (ts {lo2}..{hi2}, {(hi2-lo2)/2166667.0:.2f}s)")


main()
