"""The real Wasp at every Res mark, on one input (David, 2026-10-20: "anything above 6
might be out of the usable range anyway ... Res is not necessarily something you want to
crank up to 11"). The `test` take of lp-res0..10: the same riff, CV 1 stepping 2 -> 10 V.
Writes 16 s (8-24 s) at the recorded level, plus a second take (rep1) at 6-10, and per mark:
the clip's level, the low end at the 10 V hold against Res 0, and (6-10) how much of
the output changes from take to take (four aligned takes; each minus their average, in dB
of the whole) over the clip, the 2 -> 4 V glide and each hold. Run on the machine with
the captures: .venv-nam/bin/python tools/wasp_res_ladder.py"""
import json, pathlib
import numpy as np, soundfile as sf
from scipy.signal import welch

W = pathlib.Path.home() / "GitHub/eurorack_daisy_patch_init/daisy_neural/captures/wasp"
OUT = pathlib.Path("/tmp/res_ladder"); OUT.mkdir(exist_ok=True)
SR = 48000; A, B = 8 * SR, 24 * SR
HOLDS = {4: (10.3, 11.9), 6: (14.3, 15.9), 8: (18.3, 19.9), 10: (22.3, 23.9)}
SEGS = {"clip": (8.0, 24.0), "2 V hold": (6.3, 7.9), "2-4 V glide": (8.0, 10.2),
        **{f"{v} V hold": s for v, s in HOLDS.items()}}


def load(s):
    return np.load(W / s / "gru" / "test.npz")["y_lphp"].astype(np.float64)


def align(a, b):
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    A_, B_ = np.fft.rfft(a), np.fft.rfft(b)
    xc = np.fft.irfft(A_ * np.conj(B_), n); k = int(np.argmax(np.r_[xc[-8:], xc[:9]])) - 8
    y0, y1, y2 = (xc[(k + d) % n] for d in (-1, 0, 1)); frac = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return np.fft.irfft(B_ * np.exp(2j * np.pi * np.fft.rfftfreq(n) * frac), n)


def low_db(y, seg):
    f, p = welch(y[int(seg[0] * SR):int(seg[1] * SR)], SR, nperseg=8192)
    return 10 * np.log10(np.mean(p[(f >= 50) & (f < 200)]) + 1e-20)


rep = {}
ys = {r: load(f"lp-res{r}") for r in range(11)}
peak = max(np.abs(y[A:B]).max() for y in ys.values())
for r in range(11):
    y = ys[r]
    sf.write(OUT / f"ladder_res{r}.wav", np.clip(y[A:B], -1, 1), SR, subtype="PCM_16")
    d = {"rms_dbfs": round(float(10 * np.log10(np.mean(y[A:B] ** 2))), 1),
         "peak_dbfs": round(float(20 * np.log10(np.abs(y[A:B]).max())), 1),
         "low_vs_res0_db": round(float(low_db(y, HOLDS[10]) - low_db(ys[0], HOLDS[10])), 1)}
    if r >= 6:
        takes = [y] + [load(f"lp-res{r}-rep{k}") for k in (1, 2, 3)]
        al = [takes[0]] + [align(takes[0], t) for t in takes[1:]]
        n = min(map(len, al)); al = [t[:n] for t in al]
        mean = np.mean(al, axis=0)
        d["random_db"] = {}
        for name, (a, b) in SEGS.items():
            i, j = int(a * SR), int(b * SR)
            e_r = np.mean([np.mean((t[i:j] - mean[i:j]) ** 2) for t in al])
            e_s = np.mean([np.mean(t[i:j] ** 2) for t in al])
            d["random_db"][name] = round(float(10 * np.log10(e_r / e_s)), 1)
        sf.write(OUT / f"ladder_res{r}_take2.wav", np.clip(al[1][A:B], -1, 1), SR, subtype="PCM_16")
    rep[r] = d
    print(r, d)
print("loudest peak in the ladder:", round(float(20 * np.log10(peak)), 1), "dBFS")
(OUT / "ladder.json").write_text(json.dumps(rep, indent=1))
