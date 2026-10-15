#!/usr/bin/env python3
"""Compare two WAV segments: best lag, correlation, spectral similarity.

Usage: compare_wav.py <a.wav> <a_start_s> <a_len_s> <b.wav> <b_start_s> <b_len_s>
"""
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    return a.mean(axis=1).astype(np.float64), w.getframerate()


def main():
    pa, sa, la, pb, sb, lb = sys.argv[1:7]
    sa, la, sb, lb = float(sa), float(la), float(sb), float(lb)
    A, sr = load(pa)
    B, sr2 = load(pb)
    assert sr == sr2
    a = A[int(sa * sr):int((sa + la) * sr)]
    b = B[int(sb * sr):int((sb + lb) * sr)]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    print(f"A: {len(a)} frames rms={np.sqrt((a**2).mean()):.1f}  B: rms={np.sqrt((b**2).mean()):.1f}")
    if len(a) < 1000:
        print("too short")
        return
    # FFT-based cross-correlation
    N = 1 << (2 * n - 1).bit_length()
    fa = np.fft.rfft(a, N)
    fb = np.fft.rfft(b, N)
    cc = np.fft.irfft(fa * np.conj(fb), N)
    norm = np.sqrt((a ** 2).sum() * (b ** 2).sum())
    peak = int(np.argmax(np.abs(cc)))
    if peak > N // 2:
        peak -= N
    print(f"best lag={peak} samples ({peak/sr*1000:.1f} ms)  normalized_corr={cc[peak]/norm:.4f}")
    # spectral magnitude correlation (timbre/spatial shape, tempo-free)
    fa2 = np.abs(np.fft.rfft(a * np.hanning(n)))
    fb2 = np.abs(np.fft.rfft(b * np.hanning(n)))
    # compare in 1/3-octave bands
    f = np.fft.rfftfreq(n, 1.0 / sr)
    bands = np.zeros(40)
    for k in range(40):
        lo, hi = 50 * 2 ** (k / 3), 50 * 2 ** ((k + 1) / 3)
        idx = (f >= lo) & (f < hi)
        bands[k] = fa2[idx].mean() if idx.any() else 0
    bands2 = np.zeros(40)
    for k in range(40):
        lo, hi = 50 * 2 ** (k / 3), 50 * 2 ** ((k + 1) / 3)
        idx = (f >= lo) & (f < hi)
        bands2[k] = fb2[idx].mean() if idx.any() else 0
    c = np.corrcoef(bands, bands2)[0, 1]
    print(f"third-octave spectral correlation={c:.4f}")


main()
