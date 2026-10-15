#!/usr/bin/env python3
"""Dump the first N packets for a given status/select value from either format.

Usage:
  dump_packets.py guest <trace> <status-hex> [limit]
  dump_packets.py hw    <log>   <status-hex> [limit]
"""
import re
import sys

GUEST = re.compile(r'^\s*(\d+)\s+([0-9a-fA-F]+)\s+([WR])\s+(\d+)\s+([0-9a-fA-F]{2})')


def dump_guest(path, status, limit):
    ops = []
    for line in open(path, errors='ignore'):
        m = GUEST.match(line)
        if not m:
            continue
        ts, pc, rw, port, val = m.groups()
        ops.append((rw, int(port), int(val, 16), int(ts)))
    g = []
    for k, p, v, ts in ops:
        if k == 'W' and p == 0:
            g.append([v, [], ts])
        elif k == 'W' and p == 4 and g:
            g[-1][1].append(v)
    n = 0
    for s, d, ts in g:
        if s != status:
            continue
        print(f"sel {s:02x} ts={ts} n={len(d)} : {' '.join(f'{x:02x}' for x in d[:40])}")
        n += 1
        if n >= limit:
            break


def dump_hw(path, status, limit):
    g = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        if line.startswith('WST '):
            g.append([int(line[4:6], 16), []])
        elif line.startswith('WDT ') and g:
            g[-1][1].append(int(line[4:6], 16))
    n = 0
    for s, d in g:
        if s != status:
            continue
        print(f"WST {s:02x} n={len(d)} : {' '.join(f'{x:02x}' for x in d[:40])}")
        n += 1
        if n >= limit:
            break


def brief_guest(path, status, limit):
    ops = []
    for line in open(path, errors='ignore'):
        m = GUEST.match(line)
        if not m:
            continue
        ts, pc, rw, port, val = m.groups()
        ops.append((rw, int(port), int(val, 16), int(ts)))
    g = []
    for k, p, v, ts in ops:
        if k == 'W' and p == 0:
            g.append([v, [], ts])
        elif k == 'W' and p == 4 and g:
            g[-1][1].append(v)
    n = 0
    for s, d, ts in g:
        if s != status:
            continue
        head = ' '.join(f'{x:02x}' for x in d[:3])
        print(f"sel {s:02x} ts={ts:>10} n={len(d):>4} : {head}")
        n += 1
        if n >= limit:
            break


def brief_hw(path, status, limit):
    g = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        if line.startswith('WST '):
            g.append([int(line[4:6], 16), []])
        elif line.startswith('WDT ') and g:
            g[-1][1].append(int(line[4:6], 16))
    n = 0
    for s, d in g:
        if s != status:
            continue
        head = ' '.join(f'{x:02x}' for x in d[:3])
        print(f"WST {s:02x} n={len(d):>4} : {head}")
        n += 1
        if n >= limit:
            break


mode, path, status = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
limit = int(sys.argv[4]) if len(sys.argv) > 4 else 5
brief = '--brief' in sys.argv
if mode == 'guest':
    (brief_guest if brief else dump_guest)(path, status, limit)
else:
    (brief_hw if brief else dump_hw)(path, status, limit)
