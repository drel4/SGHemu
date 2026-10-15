#!/usr/bin/env python3
"""Report silent/active runs of a WAV, optionally annotated with trace markers.

Usage: wav_runs.py <wav> [trace] [--hop ms] [--thr frac]
"""
import sys
import wave

import numpy as np


def runs(path, hop_ms=20, thr_frac=0.02):
    w = wave.open(path)
    sr = w.getframerate()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    x = a.mean(axis=1).astype(np.float64)
    hop = max(1, int(sr * hop_ms / 1000))
    m = len(x) // hop * hop
    env = np.sqrt((x[:m].reshape(-1, hop) ** 2).mean(axis=1))
    thr = env.max() * thr_frac
    act = env > thr
    out = []
    i = 0
    while i < len(act):
        j = i
        while j < len(act) and act[j] == act[i]:
            j += 1
        out.append((act[i], i * hop_ms / 1000.0, j * hop_ms / 1000.0))
        i = j
    return out, sr, len(x), env, hop_ms / 1000.0


def main():
    path = sys.argv[1]
    trace = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else None
    hop_ms = 20
    thr_frac = 0.02
    if '--hop' in sys.argv:
        hop_ms = int(sys.argv[sys.argv.index('--hop') + 1])
    if '--thr' in sys.argv:
        thr_frac = float(sys.argv[sys.argv.index('--thr') + 1])
    out, sr, frames, env, hop_s = runs(path, hop_ms, thr_frac)
    print(f"{path}: {frames} frames @ {sr} Hz = {frames/sr:.2f}s   (silence threshold {thr_frac*100:.0f}% of peak)")
    print("runs (>=100 ms shown):")
    for is_act, t0, t1 in out:
        if t1 - t0 >= 0.1:
            print(f"  {'AUDIO' if is_act else '.....':5s} {t0:8.2f}s .. {t1:8.2f}s   ({t1-t0:6.2f}s)")
    if trace:
        print(f"markers from {trace}:")
        for line in open(trace, errors='ignore'):
            if 'AUDIO' in line:
                print("  " + line.strip())


main()
