#!/usr/bin/env python3
"""Summarize an SGHemu MA-3 traffic log (enable with trace=1 under [audio] in audio.ini).

  python tools/ma3_trace_report.py good.log            one report
  python tools/ma3_trace_report.py good.log bad.log    also show addresses only one log uses

The log holds MA-3 register traffic only: no ROM, DLL or firmware content.
"""
import collections, re, sys

LINE = re.compile(r'(\d+) ([0-9a-f]+) ([RW]) (\d) ([0-9a-f]+) sel=([0-9a-f]+)')
MARK = re.compile(r'(\d+) -+ AUDIO (pcm|silent) at frame (\d+)')

def translate(a):
    """Mirror of translate_address() in src/ma3.c. None = ignored by the translation."""
    if a < 0x90: return a
    if a < 0x140: return a + 0x30
    if 0x15a <= a <= 0x15d: return a + 0x7c
    if a in (0x148, 0x166): return 0x1df   # explicit no-op that carries a delay
    return None

def load(path):
    ev = []
    for ln in open(path, errors='replace').read().replace('\r', '').split('\n'):
        m = LINE.match(ln)
        if m: ev.append((int(m[1]), m[3], int(m[4]), int(m[5], 16), int(m[6], 16)))
    return ev

def markers(path):
    out = []
    for ln in open(path, errors='replace').read().replace('\r', '').split('\n'):
        m = MARK.match(ln)
        if m: out.append((int(m[1]), m[2], int(m[3])))
    return out

def decode(ev):
    """Rebuild FIFO packets the way delayed_packet() does; FIFO clears restart the parser."""
    packets, clears, ctrl = [], [], []
    ph = 0; st = None
    def fresh(): return dict(t=None, delay=0, dn=0, addr=0, an=0, cnt=0, cn=0, vals=[])
    st = fresh()
    for t, rw, off, v, sel in ev:
        if rw != 'W' or off != 4: continue
        s = sel & 63
        if s == 4:
            ctrl.append((t, v))
            if v & 0x20: clears.append(t); ph = 0; st = fresh()
        if s != 1: continue
        if st['t'] is None: st['t'] = t
        if ph == 0:
            st['delay'] |= (v & 127) << (7 * st['dn']); st['dn'] += 1
            if v & 128: ph = 1
        elif ph == 1:
            st['addr'] |= (v & 127) << (7 * st['an']); st['an'] += 1
            if v & 128: ph = 2 if st['an'] == 3 else 3
        elif ph == 2:
            st['cnt'] |= (v & 127) << (7 * st['cn']); st['cn'] += 1
            if v & 128: ph = 4 if st['cnt'] else 5
        elif ph == 3:
            st['vals'].append(v)
            if v & 128: ph = 5
        elif ph == 4:
            st['vals'].append(v)
            if len(st['vals']) >= st['cnt']: ph = 5
        if ph == 5:
            packets.append((st['t'], st['delay'], st['addr'], st['an'], st['vals'])); st = fresh(); ph = 0
    return packets, clears, ctrl, ph

def report(path):
    ev = load(path)
    packets, clears, ctrl, leftover = decode(ev)
    print(f"== {path}: {len(ev)} register accesses, {len(packets)} FIFO packets, "
          f"{len(clears)} FIFO clears, partial packet at end: {'yes' if leftover else 'no'}")
    short = collections.Counter(a for _, _, a, an, _ in packets if an != 3)
    ram = [p for p in packets if p[3] == 3]
    print(f"  immediate-style packets: {sum(short.values())}; 3-byte RAM packets: {len(ram)} "
          f"({sum(len(p[4]) for p in ram)} data bytes)")
    ignored = {a: n for a, n in short.items() if translate(a) is None}
    print("  addresses the translation IGNORES:", ", ".join(f"{a:03x} x{n}" for a, n in sorted(ignored.items())) or "none")
    print("  addresses used:", " ".join(f"{a:03x}" for a in sorted(short)))
    if clears:
        print("  FIFO clears at cycle:", ", ".join(map(str, clears[:12])) + (" ..." if len(clears) > 12 else ""))
    marks = markers(path)
    if marks:
        # FIFO traffic while the DLL output is silent = the sequencer is being fed but nothing is heard.
        spans = []
        for i, (t, state, frame) in enumerate(marks):
            end = marks[i + 1][0] if i + 1 < len(marks) else float('inf')
            if state == 'silent':
                real = [p for p in packets if t <= p[0] < end and translate(p[2]) not in (None, 0x1df)]
                if real: spans.append((len(real), t, end, frame))
        print(f"  audio markers: {len(marks)} state changes; "
              f"silent spans that still received real sequencer commands: {len(spans)}")
        for n, t, end, frame in sorted(spans, reverse=True)[:10]:
            print(f"    {n:4d} commands while silent, cycles {t}..{end if end != float('inf') else 'end'} (from frame {frame})")
        first = next((m for m in marks if m[1] == 'pcm'), None)
        if first:
            t0 = first[0]
            before = [p for p in packets if p[0] < t0]
            after = [p for p in packets if p[0] >= t0]
            ab = {p[2] for p in before if p[3] != 3}
            aa = {p[2] for p in after if p[3] != 3}
            print(f"  FIRST SOUND at cycle {t0} (frame {first[2]}): {len(before)} packets before, {len(after)} after")
            print("    addresses written only before the first sound:", " ".join(f"{x:03x}" for x in sorted(ab - aa)) or "none")
            print("    addresses written only after the first sound: ", " ".join(f"{x:03x}" for x in sorted(aa - ab)) or "none")
            print("    packets around it (cycle, delay, address -> chip address, values):")
            for q in before[-8:] + [None] + after[:8]:
                if q is None: print("    ---------------- first sound ----------------"); continue
                t, d, a, an, vals = q
                tr = translate(a) if an != 3 else None
                print(f"    {t:>10} delay={d:<5} addr={a:03x} -> {('%03x' % tr) if tr is not None else ('RAM' if an == 3 else 'IGNORED')}  "
                      f"{' '.join(f'{v:02x}' for v in vals[:12])}{' ...' if len(vals) > 12 else ''}")
        else:
            print("  the output never became audible in this log")
    else:
        print("  (no AUDIO markers: re-capture with a build that includes them)")
    return set(short), {p[2] >> 8 for p in ram}

if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit(__doc__)
    results = [report(p) for p in sys.argv[1:3]]
    if len(results) == 2:
        a, b = results[0][0], results[1][0]
        print(f"\nonly in {sys.argv[1]}:", " ".join(f"{x:03x}" for x in sorted(a - b)) or "none")
        print(f"only in {sys.argv[2]}:", " ".join(f"{x:03x}" for x in sorted(b - a)) or "none")
