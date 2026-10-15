#!/usr/bin/env python3
"""Locate an SMW5-rendered reference inside an emulator-rendered session WAV."""
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    return a.mean(axis=1).astype(np.float64), w.getframerate()


def main():
    emu, sr = load(sys.argv[1])
    ref, sr2 = load(sys.argv[2])
    assert sr == sr2
    hop = int(sr * 0.02)
    e = np.sqrt((emu[: len(emu) // hop * hop].reshape(-1, hop) ** 2).mean(axis=1))
    r = np.sqrt((ref[: len(ref) // hop * hop].reshape(-1, hop) ** 2).mean(axis=1))
    # normalise each to unit L1 so we compare shape, not gain
    rn = r / (r.sum() + 1e-9)
    N = len(r)
    best = None
    # coarse scan over candidate offsets
    scores = []
    for off in range(0, len(e) - N + 1):
        seg = e[off:off + N]
        s = seg / (seg.sum() + 1e-9)
        d = np.abs(s - rn).sum()
        scores.append((d, off))
    scores.sort()
    print(f"ref={sys.argv[2]} frames={len(ref)} ({len(ref)/sr:.2f}s) env_bins={N}")
    print("top offsets (L1 envelope distance, lower=better):")
    for d, off in scores[:8]:
        print(f"   t={off*0.02:8.2f}s  d={d:.4f}")
    d, off = scores[0]
    print(f"\nbest t={off*0.02:.2f}s  d={d:.4f}")
    # print first 3 s of ref vs emu@best at 100 ms
    step = 5  # 100 ms
    print("\n  t(rel)   ref_rms    emu_rms")
    for i in range(0, min(N, 30 * 50), step):
        print(f"  {i*0.02:6.2f}s {r[i]:9.1f} {e[off+i]:9.1f}")


main()
