"""Where the past-the-limits clips (notamp_ideas.py) make tones of their own: the gaps
between the phrase's notes, where the input is silent, per gap the level against the notes,
the tone's frequency and harmonics, beside where the real Wasp rings at that cutoff
(CLAUDE.md, the random part: 2^(3.1-3.5 + 0.91-0.96 V) Hz). A "13 Hz" reading 30 dB or more
down is a note's tail fading in a 70 ms window, not a tone.

    .venv-nam/bin/python tools/notamp_spectra.py
"""
import pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import notamp_ideas as N
SR = 48000
x = N.phrase(); n = len(x)
def wasp_ring(v):  # the Wasp's ring at high Res (CLAUDE.md, the random part): 2^(3.1-3.5 + 0.91-0.96 V)
    return 2 ** (3.1 + 0.91 * v), 2 ** (3.5 + 0.96 * v)
def peak(seg, lo=20, hi=20000):
    w = seg * np.hanning(len(seg)); P = np.abs(np.fft.rfft(w, 8 * len(seg))); f = np.fft.rfftfreq(8 * len(seg), 1 / SR)
    m = (f > lo) & (f < hi); k = np.argmax(np.where(m, P, 0))
    a, b, c = np.log(P[k - 1:k + 2] + 1e-30); d = 0.5 * (a - c) / (a - 2 * b + c)
    f0 = (k + d) * SR / (8 * len(seg))
    harm = []
    for h in range(2, 6):
        mm = (f > h * f0 * 0.97) & (f < h * f0 * 1.03)
        harm.append(20 * np.log10(P[mm].max() / P[k] + 1e-12) if mm.any() and h * f0 < 20000 else np.nan)
    return f0, harm
cases = {
  "f1 reference (Res 6, cutoff 2->8 V)": (np.linspace(2, 8, n), np.full(n, 6.0), 0.0),
  "f3 Res 16 (cutoff 2->8 V)": (np.linspace(2, 8, n), np.full(n, 16.0), 0.0),
  "f5 cutoff 8->14 V (Res 6)": (np.linspace(8, 14, n), np.full(n, 6.0), 0.0),
  "f8 feedback 0.95 (Res 8, cutoff 2->8 V)": (np.linspace(2, 8, n), np.full(n, 8.0), 0.95),
}
for name, (cut, res, fb) in cases.items():
    y = N.run_gru(x, cut, res, fb)
    notes = np.sqrt(np.mean(y[np.abs(x) > 0.05] ** 2))
    print(f"\n{name}")
    print("  gap  cutoff V   level in the gap   tone Hz    Wasp ring Hz     H2    H3    H4    H5 (dB re tone)")
    for k in range(16):
        a, b = int((k * 0.5 + 0.425) * SR), int((k * 0.5 + 0.495) * SR)
        seg = y[a:b]
        lvl = 20 * np.log10(np.sqrt(np.mean(seg ** 2)) / notes + 1e-12)
        v = cut[(a + b) // 2]
        if lvl < -40:
            print(f"  {k:2d}   {v:5.2f}     {lvl:6.1f} dB          (silent)"); continue
        f0, h = peak(seg)
        lo, hi = wasp_ring(v)
        hs = " ".join(f"{q:5.0f}" if np.isfinite(q) else "    -" for q in h)
        print(f"  {k:2d}   {v:5.2f}     {lvl:6.1f} dB      {f0:8.0f}   {lo:6.0f}-{hi:<6.0f}  {hs}")
