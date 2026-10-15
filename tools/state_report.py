"""Summarise a MA5HW_STATE dump next to a WAV's audible runs.

Usage: python tools\\state_report.py state.log [wav]

Prints one line per dump: output time, delayed-FIFO fill, active voices,
dirty (stale ADPCM cache) voices, and whether any voice phase advanced since
the previous dump. A voice whose phase does not move while active is a stall.
"""
import sys
import wave
import struct


def parse_state(path):
    blocks = []
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("GEN "):
            if cur:
                blocks.append(cur)
            cur = {"frames": int(line[4:]), "fill": None, "size": None,
                   "voices": []}
        elif line.startswith("FIFO ") and cur is not None:
            cur["size"] = int(line.split("size=")[1].split()[0])
            cur["fill"] = int(line.split("fill=")[1].split()[0])
        elif line.startswith("V") and cur is not None:
            f = dict(kv.split("=") for kv in line.split()[1:])
            cur["voices"].append((line.split()[0], f))
    if cur:
        blocks.append(cur)
    return blocks


def runs(path, thresh, win=2400):
    w = wave.open(path, "rb")
    n = w.getnframes()
    data = w.readframes(n // win * win)
    frames = n // win
    out = []
    prev = None
    for i in range(frames):
        chunk = data[i * win * 4:(i + 1) * win * 4]
        vals = struct.unpack("<%dh" % (win * 2), chunk)
        peak = max(abs(v) for v in vals)
        on = peak > thresh
        t = (i * win) / 48000.0
        if on != prev:
            out.append((t, "AUDIO" if on else "silent"))
            prev = on
    return frames * win, out


def main():
    blocks = parse_state(sys.argv[1])
    if len(sys.argv) > 2:
        total, en = runs(sys.argv[2], 8)
        print("WAV %d frames = %.2fs" % (total, total / 48000.0))
        for t, what in en:
            print("  %7.2fs  %s" % (t, what))
    print("t     fill  act dirty  moved  phases")
    last = {}
    for b in blocks:
        t = b["frames"] / 48000.0
        act = [v for v in b["voices"] if int(v[1]["act"], 16) & 255]
        dirty = [v for v in act if v[1]["d"] == "1"]
        moved = 0
        for name, f in act:
            p = int(f["ph"])
            if name in last and p != last[name]:
                moved += 1
            last[name] = p
        for name, f in act:
            last.setdefault(name, int(f["ph"]))
        print("%6.2f %5s %4d %5d %6d" % (t, b["fill"], len(act), len(dirty), moved))


if __name__ == "__main__":
    main()
