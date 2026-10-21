"""Swords above the threshold: what the self-oscillation is, and what repeats take to take.
A: no input (step 0's Res ramps). B: the riff (sw-a test takes, three back-to-back reps).

    .venv/bin/python tools/swords_osc.py
"""
import json, pathlib, sys
import numpy as np
import soundfile as sf
from scipy import signal

D = pathlib.Path(__file__).resolve().parents[1] / "captures/swords"
SR = 48000
rig = json.loads((D / "rig.json").read_text())
cal = json.loads((D / "cal_result.json").read_text())
meta = json.loads((D / "cal.json").read_text())
fit = cal["fit"]
fc = lambda v: 2 ** (fit["log2_hz_at_0v"] + fit["octaves_per_volt"] * v)  # noqa: E731


def peak_hz(x, lo=5.0, hi=20000.0, pad=8):
    n = len(x) * pad
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    f = np.fft.rfftfreq(n, 1 / SR)
    m = (f > lo) & (f < hi)
    k = np.argmax(np.where(m, s, 0))
    a, b, c = np.log(s[k - 1:k + 2] + 1e-30)
    d = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + d) * SR / n, s, f


def harmonics(x, f0, n=6):
    _, s, f = peak_hz(x)
    out = []
    for h in range(1, n + 1):
        m = (f > h * f0 * 0.97) & (f < h * f0 * 1.03)
        out.append(20 * np.log10(s[m].max() + 1e-30) if m.any() else np.nan)
    return np.array(out) - out[0]


print("A. No input: Swords oscillating on its own (Response CCW, Res CV ramping 0 -> 8 -> 0 V)")
play, _ = sf.read(D / "cal.wav", dtype="float64", always_2d=True)
rec, _ = sf.read(D / "cal.rec.wav", dtype="float64", always_2d=True)
lag, pa = cal["latency"], meta["payload_at"]
F1, RES = 0, 3
for p in meta["parts"]:
    if p["kind"] != "res_ramp" or p["cut_v"] < 0:
        continue
    a = pa + int(p["t0"] * SR)
    b = pa + int(p["t1"] * SR)
    y = rec[a + lag:b + lag, F1] * rig["in_vfs"]
    rv = play[a:b, RES] * rig["out_vfs"]
    w = SR // 4  # quarter-second frames
    rows = []
    for i in range(0, len(y) - w, w):
        seg = y[i:i + w]
        if np.sqrt(np.mean(seg ** 2)) < 0.3:
            continue
        f0, _, _ = peak_hz(seg)
        if f0 * w / SR < 8:  # under 8 cycles in the frame: too few to read
            continue
        rows.append((rv[i:i + w].mean(), f0, np.abs(seg).max(), np.abs(seg).max() / np.sqrt(np.mean(seg ** 2)),
                     harmonics(seg, f0)))
    if not rows:
        print(f"  V/OCT {p['cut_v']:+.0f} V: no readable oscillation")
        continue
    r = np.array([x[0] for x in rows]); f = np.array([x[1] for x in rows])
    pk = np.array([x[2] for x in rows]); cr = np.array([x[3] for x in rows])
    H = np.array([x[4] for x in rows])
    top = np.argmax(r)
    print(f"  V/OCT {p['cut_v']:+.0f} V (the -3 dB cutoff at Res 0 there: {fc(p['cut_v']):.0f} Hz)")
    print(f"    oscillates at {f.min():.1f}-{f.max():.1f} Hz over Res {r.min():.2f}-{r.max():.2f} V"
          f" ({1200 * np.log2(f.max() / f.min()):.0f} cents of movement), {f[top]:.1f} Hz at the top")
    print(f"    level {pk.min():.1f}-{pk.max():.1f} V peak; crest {cr.min():.2f}-{cr.max():.2f} (a sine is 1.41, a square 1.0)")
    print("    harmonics, dB re the fundamental (H2..H6), at the onset / at the top:")
    print("      " + " ".join(f"{v:6.1f}" for v in H[0][1:]) + "   /   " + " ".join(f"{v:6.1f}" for v in H[top][1:]))
    # Tightness: a 2 s window at the top of the ramp; the -3 dB width of the fundamental
    # against the window's own (a Hann main lobe is 1.44 bins wide at -3 dB).
    mid = len(y) // 2
    seg = y[mid - SR:mid + SR]
    n = len(seg) * 16
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)); ff = np.fft.rfftfreq(n, 1 / SR)
    k = np.argmax(s[ff > 5]) + np.argmax(ff > 5)
    above = np.where(s > s[k] / np.sqrt(2))[0]
    lo_, hi_ = k, k
    while lo_ - 1 in above: lo_ -= 1
    while hi_ + 1 in above: hi_ += 1
    width = (hi_ - lo_) * SR / n
    # Drift: the pitch in 50 ms frames across the same 2 s, Res ramping slowly through the top.
    fr = [peak_hz(seg[j:j + SR // 20])[0] for j in range(0, len(seg) - SR // 20, SR // 20)] if f[top] > 200 else []
    print(f"    the peak at the top: -3 dB width {width:.2f} Hz over 2 s (window alone 0.72 Hz)"
          + (f"; pitch in 50 ms frames {np.std(1200 * np.log2(np.array(fr) / np.median(fr))):.1f} cents rms" if fr else ""))

print("\nB. The riff: three back-to-back takes per Res, per hold of the cutoff (4 s holds)")
s = json.loads((D / "sw-a/session.json").read_text())
holds = s["test_holds_v"]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from swords_capture import align  # noqa: E402


def stft_mag(x):
    _, _, Z = signal.stft(x, SR, nperseg=2048, noverlap=1536, window="hann")
    return np.abs(Z)


def bands_db(x):
    f, p = signal.welch(x, SR, nperseg=8192)
    e = 20 * 2 ** (np.arange(0, 120) / 12)
    out = np.array([p[(f >= a) & (f < b)].sum() for a, b in zip(e[:-1], e[1:])])
    return 10 * np.log10(out + 1e-30)


print("  waveform ESR / magnitude ESR (2048-pt STFT, 43 ms frames) / long-term band levels, mean |dB| difference")
print("  (rep 1 v 2 and 2 v 3, averaged; band levels over bands within 50 dB of the loudest)")
peaks = {}
for r in (3.5, 4.0, 4.5, 5.5):
    reps = [np.load(D / f"sw-a/gru/test_r{r:+.2f}_{k}.npz")["y_f1"].astype(np.float64) for k in (1, 2, 3)]
    al = [reps[0]] + [align(reps[0], t) for t in reps[1:]]
    cells = []
    for i, hv in enumerate(holds):
        a, b = int((4 * i + 0.5) * SR), int((4 * i + 3.9) * SR)
        we, me, bd = [], [], []
        for j in range(2):
            x1, x2 = al[j][a:b], al[j + 1][a:b]
            we.append(np.sum((x1 - x2) ** 2) / np.sum(x1 ** 2))
            m1, m2 = stft_mag(x1), stft_mag(x2)
            me.append(np.sum((m1 - m2) ** 2) / np.sum(m1 ** 2))
            b1, b2 = bands_db(x1), bands_db(x2)
            keep = b1 > b1.max() - 50
            bd.append(np.mean(np.abs(b1 - b2)[keep]))
        cells.append((np.mean(we), np.mean(me), np.mean(bd)))
        peaks[(r, i)] = [peak_hz(al[j][a:b], lo=15)[0] for j in range(3)]
    print(f"  Res {r:3.1f} V  " + "  ".join(f"{hv:+5.1f}V {c[0]:5.3f}/{c[1]:5.3f}/{c[2]:4.1f}" for hv, c in zip(holds, cells)))

print("\n  the strongest peak per hold, Hz, in each of the three takes (Res 3.5 V for comparison)")
for r in (3.5, 4.5, 5.5):
    print(f"  Res {r:3.1f} V  " + "  ".join("/".join(f"{v:.0f}" for v in peaks[(r, i)]) for i in range(len(holds))))
