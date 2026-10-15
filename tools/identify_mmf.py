#!/usr/bin/env python3
"""Identify which MMF the firmware trace played by matching RAM-write payloads
against SMW5 reference captures of known files.

Usage: identify_mmf.py <guest-trace> <ref1.log> <ref2.log> ...
"""
import re
import sys
from collections import Counter

GUEST = re.compile(r'^\s*(\d+)\s+([0-9a-fA-F]+)\s+([WR])\s+(\d+)\s+([0-9a-fA-F]{2})')


def guest_data_stream(path):
    """All port-4 data bytes the firmware wrote, in order, grouped per sel op."""
    ops = []
    for line in open(path, errors='ignore'):
        m = GUEST.match(line)
        if not m:
            continue
        ts, pc, rw, port, val = m.groups()
        ops.append((rw, int(port), int(val, 16)))
    groups, cur = [], None
    for k, p, v in ops:
        if k == 'W' and p == 0:
            cur = (v, [])
            groups.append(cur)
        elif k == 'W' and p == 4 and cur is not None:
            cur[1].append(v)
    return groups


def ref_writes(path):
    """SMW5 capture: return list of (status, [data...]) ops."""
    g = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        if line.startswith('WST '):
            g.append([int(line[4:6], 16), []])
        elif line.startswith('WDT ') and g:
            g[-1][1].append(int(line[4:6], 16))
    return g


def main():
    guest = sys.argv[1]
    refs = sys.argv[2:]
    groups = guest_data_stream(guest)
    pool = bytearray()
    for sel, data in groups:
        if len(data) >= 16:
            pool += bytes(data)
    print(f"guest: {len(groups)} ops, pool {len(pool)} data bytes (ops with >=16 bytes)")

    def is_distinctive(b, need):
        return len(set(b)) >= need

    for win_size, need in ((32, 8), (48, 10), (64, 12), (96, 14)):
        pool_set = set()
        for i in range(len(pool) - win_size):
            w = bytes(pool[i:i + win_size])
            if is_distinctive(w, need):
                pool_set.add(w)
        print(f"\n-- window {win_size} (>= {need} distinct values), guest has {len(pool_set)} --")
        for ref in refs:
            w = ref_writes(ref)
            windows = set()
            for sel, data in w:
                if len(data) >= win_size:
                    b = bytes(data)
                    for i in range(len(b) - win_size):
                        win = b[i:i + win_size]
                        if is_distinctive(win, need):
                            windows.add(win)
            hit = windows & pool_set
            print(f"   {ref}: {len(windows):>6} windows, {len(hit):>5} matched "
                  f"({100.0*len(hit)/max(1,len(windows)):5.1f}%)")


main()
