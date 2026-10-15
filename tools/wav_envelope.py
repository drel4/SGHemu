#!/usr/bin/env python3
"""Find the silent/active structure of a rendered WAV and locate the ringtone."""
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    n = w.getnframes()
    raw = w.readframes(n)
    a = np.frombuffer(raw, dtype='<i2').reshape(-1, w.getnchannels())
    return a.mean(axis=1).astype(np.float64), w.getframerate()


def envelope(x, hop):
    m = len(x) // hop * hop
    return np.sqrt((x[:m].reshape(-1, hop) ** 2).mean(axis=1))


def main():
    emu, sr = load(sys.argv[1])
    hop = int(sr * 0.02)  # 20 ms
    env = envelope(emu, hop)
    print(f"emu frames={len(emu)} rate={sr} env_bins={len(env)} hop={hop}")
    # print a coarse activity timeline: rms per 250 ms, compact
    big = envelope(emu, int(sr * 0.25))
    thr = big.max() * 0.02
    line = []
    for i, v in enumerate(big):
        line.append('.' if v < thr else ('#' if v > big.max() * 0.5 else '+'))
    s = ''.join(line)
    print("activity (250ms/bin, '.'=silent '+'/ '#'=audio):")
    for i in range(0, len(s), 120):
        print(f"  {i*0.25:7.1f}s {s[i:i+120]}")
    # first 1.0 s detail at 20 ms
    print("first 1.2 s rms per 20 ms:")
    for i in range(min(60, len(env))):
        print(f"   {i*20:5d}ms {env[i]:10.1f}")


main()
