#!/usr/bin/env python3
"""
wasp_wildness.py — what the Wasp's wildness is (CLAUDE.md, "What the Wasp's wildness is:
three tests"), on the four recordings of identical input at Res 6-10 (lp-res<r> and its
-rep1..3), LP:

1. Phase only, or the spectrum too? Per Res and cutoff hold: the take-to-take error of the
   waveform, of the magnitude of a 43 ms STFT, and of the band levels over the hold. Swords
   above its threshold: waveform 0.2-2.0, magnitude under 0.03, bands within 0.5 dB.
2. Noise or chaos? After each rest in the test riff the Wasp sits still, so every note
   starts from the same state. The takes' difference, relative to the signal, in windows
   after each such onset: from the first milliseconds at a steady level is noise amplified
   by a nearly undamped ring; starting near nothing and growing over the note is chaos.
3. Is the warble beating? Per hold, in 0.5 s frames: the gain at the resonance (output over
   input envelope, a third of an octave around the ring) and its strongest wobble at 2-40
   Hz, against the gap between the ring and the input's partial nearest it.

    .venv-nam/bin/python tools/wasp_wildness.py     # on the mini, with the Crucial attached
"""

import pathlib
import sys

import numpy as np
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import control_response as C  # noqa: E402
from swords_capture import align  # noqa: E402

SR = 48000
W = HERE.parent / "captures/wasp"
RES = [6, 7, 8, 9, 10]


def takes(r):
    names = [f"lp-res{r}"] + [f"lp-res{r}-rep{k}" for k in (1, 2, 3)]
    z = [np.load(W / n / "gru/test.npz") for n in names if (W / n / "gru/test.npz").exists()]
    ys = [t["y_lphp"].astype(np.float64) for t in z]
    al = [ys[0]] + [align(ys[0], y) for y in ys[1:]]
    n = min(map(len, al))
    return z[0]["x"].astype(np.float64)[:n], z[0]["cv_v"].astype(np.float64)[:n], [y[:n] for y in al]


def ring_hz(v):
    return 2 ** (3.3 + 0.935 * v)  # the middle of the Wasp's ring fit (the random part)


def test1(data):
    print("1. phase only or the spectrum too: waveform ESR / STFT magnitude ESR / band levels mean |dB|")
    print("   Res  " + "".join(f"{v:>22d}V" for v in C.HOLDS))
    for r in RES:
        x, cv, ys = data[r]
        cells = []
        for v, (a, b) in C.HOLDS.items():
            seg = [y[int(a * SR):int(b * SR)] for y in ys]
            we, me, bd = [], [], []
            for i in range(len(seg)):
                for j in range(i + 1, len(seg)):
                    p, q = seg[i], seg[j]
                    we.append(np.sum((p - q) ** 2) / np.sum(p ** 2))
                    _, _, P = signal.stft(p, SR, nperseg=2048, noverlap=1536)
                    _, _, Q = signal.stft(q, SR, nperseg=2048, noverlap=1536)
                    me.append(np.sum((np.abs(P) - np.abs(Q)) ** 2) / np.sum(np.abs(P) ** 2))
                    f, pp = signal.welch(p, SR, nperseg=8192)
                    _, qq = signal.welch(q, SR, nperseg=8192)
                    e = 40 * 2 ** (np.arange(0, 109) / 12)
                    bp = np.array([10 * np.log10(pp[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in zip(e[:-1], e[1:])])
                    bq = np.array([10 * np.log10(qq[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in zip(e[:-1], e[1:])])
                    keep = bp > bp.max() - 50
                    bd.append(np.mean(np.abs(bp - bq)[keep]))
            cells.append(f"{np.mean(we):6.3f} /{np.mean(me):6.3f} /{np.mean(bd):4.1f}")
        print(f"   {r:3d}  " + "  ".join(cells))


def onsets(x):
    env = np.abs(signal.hilbert(x))
    env = np.convolve(env, np.ones(48) / 48, mode="same")
    quiet, loud = 0.01 * env.max(), 0.1 * env.max()
    out, i, n = [], int(0.5 * SR), len(x)
    while i < n - SR:
        if env[i] > loud and np.all(env[i - int(0.02 * SR):i - int(0.005 * SR)] < quiet):
            out.append(i - int(0.005 * SR))
            i += int(0.25 * SR)
        i += 48
    return out


def test2(data):
    wins = [(0, 5), (5, 20), (20, 50), (50, 100), (100, 200)]
    print("\n2. noise or chaos: takes' difference relative to the signal after a rest, dB, by ms after the onset")
    print("   Res  onsets  " + "  ".join(f"{a:>3d}-{b:<3d}ms" for a, b in wins))
    for r in RES:
        x, cv, ys = data[r]
        on = onsets(x)
        mean = np.mean(ys, axis=0)
        rows = []
        for a, b in wins:
            num = den = 0.0
            for o in on:
                s, e = o + int(a * SR / 1000), o + int(b * SR / 1000)
                for y in ys:
                    num += np.sum((y[s:e] - mean[s:e]) ** 2)
                    den += np.sum(mean[s:e] ** 2)
            rows.append(10 * np.log10(num / (den + 1e-30) + 1e-30))
        print(f"   {r:3d}   {len(on):4d}   " + "  ".join(f"{v:9.1f}" for v in rows))


def test3(data):
    print("\n3. the warble as beating: per 0.5 s frame at the holds, the gain wobble's rate at the ring"
          " against the gap to the input's nearest partial (Hz)")
    for r in (8, 9, 10):
        x, cv, ys = data[r]
        meas, pred = [], []
        for v, (a, b) in C.HOLDS.items():
            fr = ring_hz(v)
            if fr > 16000:
                continue
            sos = signal.butter(4, [fr * 2 ** (-1 / 3), min(fr * 2 ** (1 / 3), 20000)], "bandpass", fs=SR, output="sos")
            for t0 in np.arange(a, b - 0.5 + 1e-9, 0.5):
                s, e = int(t0 * SR), int((t0 + 0.5) * SR)
                ex = np.abs(signal.hilbert(signal.sosfiltfilt(sos, x[s:e])))
                if ex.mean() < 1e-4:
                    continue
                for y in ys[:2]:
                    ey = np.abs(signal.hilbert(signal.sosfiltfilt(sos, y[s:e])))
                    g = 20 * np.log10(np.maximum(ey, 1e-6) / np.maximum(ex, 1e-6))
                    g = signal.decimate(g - g.mean(), 24)
                    f, p = signal.welch(g, SR / 24, nperseg=len(g))
                    band = (f >= 2) & (f <= 40)
                    meas.append(f[band][np.argmax(p[band])])
                    X = np.abs(np.fft.rfft(x[s:e] * np.hanning(e - s)))
                    fx = np.fft.rfftfreq(e - s, 1 / SR)
                    m = (fx > fr / 2) & (fx < fr * 2)
                    pk = fx[m][np.argsort(X[m])[-5:]]  # the strongest partials near the ring
                    pred.append(np.min(np.abs(pk - fr)))
        meas, pred = np.array(meas), np.array(pred)
        ok = pred <= 40
        rr = np.corrcoef(meas[ok], pred[ok])[0, 1] if ok.sum() > 3 else float("nan")
        near = np.mean(np.abs(meas[ok] - pred[ok]) <= 2) if ok.any() else float("nan")
        print(f"   Res {r}: {len(meas)} frames; where the gap is under 40 Hz ({ok.sum()}): correlation {rr:+.2f}, "
              f"wobble within 2 Hz of the gap in {near:.0%}; wobble median {np.median(meas):.1f} Hz")


def main():
    data = {r: takes(r) for r in RES}
    print("takes per Res:", {r: len(data[r][2]) for r in RES}, flush=True)
    test1(data)
    test2(data)
    test3(data)


if __name__ == "__main__":
    main()
