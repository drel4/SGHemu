#!/usr/bin/env python3
"""Analyze MA-3 guest traffic traces and MA-5 HW-DLL FIFO captures.

Two input formats are understood:

  guest :  "<ts> <pc> W <port> <val> sel=<sel>"   (port 0 = status flag reg,
                                                     port 4 = data reg)
  hw    :  "WST <val>" / "WDT <val>"              (proxy or emulator capture)

Usage:
  fifo_analyze.py guest <trace> [--ops]
  fifo_analyze.py hw    <log>   [--ops]
"""
import sys
import re
from collections import Counter

GUEST = re.compile(r'^\s*(\d+)\s+([0-9a-fA-F]+)\s+([WR])\s+(\d+)\s+([0-9a-fA-F]{2})')


def parse_guest(path):
    ops = []          # (kind, reg, value)
    for line in open(path, errors='ignore'):
        m = GUEST.match(line)
        if not m:
            continue
        ts, pc, rw, port, val = m.groups()
        ops.append((rw, int(port), int(val, 16), int(ts)))
    return ops


def parse_hw(path):
    ops = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        if line.startswith('WST '):
            ops.append(('W', 0, int(line[4:6], 16)))
        elif line.startswith('WDT '):
            ops.append(('W', 4, int(line[4:6], 16)))
        elif line.startswith('GEN'):
            ops.append(('G', 0, 0))
        elif line.startswith('INIT'):
            ops.append(('I', 0, 0))
    return ops


def group_hw(ops):
    """Group into (status, [data...]) operations."""
    out = []
    for kind, port, val in ops:
        if kind == 'W' and port == 0:
            out.append([val, []])
        elif kind == 'W' and port == 4 and out:
            out[-1][1].append(val)
    return out


def summary(name, ops):
    print(f"=== {name}: {len(ops)} events ===")
    simple = [(o[0], o[1], o[2]) for o in ops]
    wr = Counter(k for k, p, v in simple)
    print("  kinds:", dict(wr))
    print("  port0 values:", sorted(Counter(v for k, p, v in simple if k == 'W' and p == 0).items()))
    print("  port4 values: %d distinct, %d total" % (
        len(set(v for k, p, v in simple if k == 'W' and p == 4)),
        sum(1 for k, p, v in simple if k == 'W' and p == 4)))


def main():
    mode, path = sys.argv[1], sys.argv[2]
    show = '--ops' in sys.argv
    if mode == 'guest':
        ops = parse_guest(path)
        summary(path, ops)
        # group: port0 write starts an op, port4 writes are its data
        g = []
        for k, p, v, ts in ops:
            if k == 'W' and p == 0:
                g.append([v, []])
            elif k == 'W' and p == 4 and g:
                g[-1][1].append(v)
        shapes = Counter((s, len(d)) for s, d in g)
        print("  op shapes (status, ndata) top:")
        for (s, n), cnt in shapes.most_common(30):
            print(f"    sel {s:02x} + {n} data  x{cnt}")
        if show:
            for s, d in g[:200]:
                ds = ' '.join(f'{x:02x}' for x in d[:24])
                if len(d) > 24:
                    ds += f' ...(+{len(d)-24})'
                print(f"  sel {s:02x} : {ds}")
        return
    ops = parse_hw(path)
    summary(path, ops)
    g = group_hw(ops)
    shapes = Counter((s, len(d)) for s, d in g)
    print("  op shapes (status, ndata) top:")
    for (s, n), cnt in shapes.most_common(30):
        print(f"    WST {s:02x} + {n} data  x{cnt}")
    if show:
        for s, d in g[:120]:
            ds = ' '.join(f'{x:02x}' for x in d[:24])
            if len(d) > 24:
                ds += f' ...(+{len(d)-24})'
            print(f"  WST {s:02x} : {ds}")
    if '--firstbulk' in sys.argv:
        idx = next((i for i, (s, d) in enumerate(g) if len(d) >= 128), None)
        if idx is not None:
            lo, hi = max(0, idx - 25), min(len(g), idx + 25)
            for i in range(lo, hi):
                s, d = g[i]
                ds = ' '.join(f'{x:02x}' for x in d[:20])
                if len(d) > 20:
                    ds += f' ...(+{len(d)-20})'
                print(f"  [{i}] WST {s:02x} : {ds}")




main()
