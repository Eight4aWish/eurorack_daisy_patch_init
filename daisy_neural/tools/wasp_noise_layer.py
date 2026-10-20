"""Can the Wasp's randomness at high Res be layered on top of a network? (David, 2026-10-20)

Per Res mark (9, 10), on the `test` take, identical input in four recordings
(lp-res<N> and lp-res<N>-rep1..3), aligned. Their average is the repeatable part, what a
perfect network would play; each take minus it is the random part.

Learned from one part of the take, judged on another. Everything a layer uses (the
granular bank, the random-phase table, the CV -> cutoff fit) comes from 24-56 s only, a
slow sweep 0 -> 10 -> 0 V. Every clip and score is from 8-24 s, the riff stepping
2 -> 10 V in held steps. (The first round, 20 Oct morning, drew its bank and table from the
clip itself.)

Clips, each on two bases, the average (`mean_`) and the first-recipe cutoff + Res GRU-24
(`gru_`, lp_cr_gru24_s2, Mirth's f1):
  g1     the granular EQ as first heard: David's idea, no added signal. A bank of how
         takes 2-4 lift and dip against the average, in twelfth-octave bands frame by
         frame, filed by octaves from the cutoff; 8-frame grains (~85 ms) drawn at random,
         two staggered crossfaded streams, applied as gains to the spectrum underneath,
         its phase kept; gains limited to +-12 dB
  g2     gentler: 16-frame grains (~170 ms), +-9 dB, the gains smoothed over a quarter
         octave, each grain drawn from a moment whose CV is within 1 V of the target's
  rp     a random-phase copy of the base, scaled per bin by how random the Wasp is at
         that distance from the cutoff
  both   rp added, then g2 over the sum
  mean_oracle  the average + noise shaped to the measured random part of the clip itself:
         the most a layer could give, in-sample by construction
Writes 16-bit WAVs of 8-24 s at one gain per mark to /tmp/noise_layer, a report.json, and
prints each clip's band gap: mean |dB| between its octave-band levels and the four takes'
average at the 4-10 V holds, over seeds 1-3 (audio from seed 1). Run on the machine with
the captures:
  .venv-nam/bin/python tools/wasp_noise_layer.py"""
import json, pathlib, sys
import numpy as np, soundfile as sf, torch
from scipy.ndimage import uniform_filter1d
from scipy.signal import welch, stft, istft

DN = pathlib.Path.home() / "GitHub/eurorack_daisy_patch_init/daisy_neural"
sys.path.insert(0, str(DN / "tools")); import train_gru as T
W = DN / "captures" / "wasp"; OUT = pathlib.Path("/tmp/noise_layer"); OUT.mkdir(exist_ok=True)
SR = 48000; A, B = 8 * SR, 24 * SR          # the clip: everything is judged here
FIT = (24.0, 56.0)                          # the sweep: everything is learned here
HOLDS = {4: (10.3, 11.9), 6: (14.3, 15.9), 8: (18.3, 19.9), 10: (22.3, 23.9)}
BANDS = ((50, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000))
SEEDS = (1, 2, 3)
HOP, NFFT = 512, 2048


def align(a, b):
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    A_, B_ = np.fft.rfft(a), np.fft.rfft(b)
    xc = np.fft.irfft(A_ * np.conj(B_), n); k = int(np.argmax(np.r_[xc[-8:], xc[:9]])) - 8
    y0, y1, y2 = (xc[(k + d) % n] for d in (-1, 0, 1)); frac = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return np.fft.irfft(B_ * np.exp(2j * np.pi * np.fft.rfftfreq(n) * frac), n)


def psd(y, seg):
    return welch(y[int(seg[0] * SR):int(seg[1] * SR)], SR, nperseg=8192)


def bands(y, seg):
    f, p = psd(y, seg)
    return np.array([10 * np.log10(np.mean(p[(f >= lo) & (f < hi)]) + 1e-20) for lo, hi in BANDS])


def spec(y):
    return stft(y, SR, nperseg=NFFT, noverlap=NFFT - HOP)


m = T.RNNModel(3, 24); m.load_state_dict(torch.load(W / "gru_runs/lp_cr_gru24_s2/best.pt", map_location="cpu")); m.eval()
report = {}
for r in (9, 10):
    names = [f"lp-res{r}", f"lp-res{r}-rep1", f"lp-res{r}-rep2", f"lp-res{r}-rep3"]
    takes = [np.load(W / s / "gru" / "test.npz")["y_lphp"].astype(np.float64) for s in names]
    al = [takes[0]] + [align(takes[0], t) for t in takes[1:]]
    n = min(map(len, al)); al = [t[:n] for t in al]
    mean = np.mean(al, axis=0); resid = [t - mean for t in al]
    cv = np.load(W / names[0] / "gru" / "test.npz")["cv_v"][:n].astype(np.float64)
    take = T.Take(W / names[0] / "gru" / "test.npz"); take.res = float(r)
    _, o = T.evaluate(m, [(take, 0, take.n, T.SETTLE, False)], ["audio", "cv", "res"], "lphp", "cpu", keep=True)
    gru = o[0][:n].astype(np.float64)

    # One-second windows of the sweep at 3 V and up (below that the cutoff is under
    # ~100 Hz and the random part is ~-30 dB): where the random part peaks gives the
    # CV -> cutoff fit, and its share of each band against distance from the cutoff
    # gives the random-phase table.
    wins = [(t0, t0 + 1.0) for t0 in np.arange(FIT[0], FIT[1] - 1.0 + 1e-9, 1.0)]
    vs, hz = [], []
    for seg in wins:
        v = float(np.mean(cv[int(seg[0] * SR):int(seg[1] * SR)]))
        if v < 3:
            continue
        f, _ = psd(resid[0], seg)
        prs = np.convolve(np.mean([psd(t, seg)[1] for t in resid], axis=0), np.ones(5) / 5, mode="same")
        band = (f > 40) & (f < 16000)
        vs.append(v); hz.append(float(f[band][np.argmax(prs[band])]))
    b, a = np.polyfit(vs, np.log2(hz), 1)  # log2 f = a + b V
    xs_, rho_ = [], []
    for seg in wins:
        v = float(np.mean(cv[int(seg[0] * SR):int(seg[1] * SR)]))
        if v < 3:
            continue
        f_c = 2 ** (a + b * v)
        er = np.mean([10 ** (bands(t, seg) / 10) for t in resid], axis=0)
        es = np.mean([10 ** (bands(t, seg) / 10) for t in al], axis=0)
        for (lo, hi), e1, e2 in zip(BANDS, er, es):
            xs_.append(np.log2(np.sqrt(lo * hi) / f_c)); rho_.append(min(0.95, e1 / e2))
    order = np.argsort(xs_); xs_ = np.array(xs_)[order]; rho_ = np.array(rho_)[order]
    rho_s = np.array([np.median(rho_[max(0, i - 4):i + 5]) for i in range(len(rho_))])  # running median of 9

    # The random-phase layer (rp).
    fr, tt, G = stft(gru, SR, nperseg=NFFT)
    fc_t = 2 ** (a + b * np.clip(np.interp(tt * SR, np.arange(n), cv), 0, 10))
    X = np.log2(np.maximum(fr[:, None], 1.0) / fc_t[None, :])
    rho_grid = np.interp(X, xs_, rho_s, left=rho_s[0], right=rho_s[-1])
    gain_grid = np.sqrt(rho_grid / (1 - rho_grid))

    def rp(base, rng):
        _, _, Gb = stft(base, SR, nperseg=NFFT)
        ph = np.exp(2j * np.pi * rng.random(Gb.shape))
        return base + istft(np.abs(Gb) * gain_grid * ph, SR, nperseg=NFFT)[1][:n]

    # The granular bank: per twelfth-octave band and frame, each of takes 2-4 against the
    # average, in dB. Bands, not FFT bins: a near-empty bin makes the ratio of two tiny
    # numbers swing wildly (a first version per bin scored worse than no layer).
    fq, tq, Zm = spec(mean)
    edges = 40 * 2 ** (np.arange(0, 9 * 12 + 1) / 12); edges = edges[edges < 20000]
    centres = np.sqrt(edges[:-1] * edges[1:]); lc = np.log2(centres)
    which = np.digitize(fq, edges) - 1

    def band_power(Z):
        P = np.abs(Z) ** 2
        out = np.full((len(centres), Z.shape[1]), 1e-18)
        for i in range(len(centres)):
            sel = which == i
            if sel.any():
                out[i] = P[sel].mean(axis=0) + 1e-18
        return out

    Pm = band_power(Zm)
    raw = np.stack([10 * np.log10(band_power(spec(t)[2]) / Pm) for t in al[1:]])
    smooth = uniform_filter1d(raw, 3, axis=1, mode="nearest")  # quarter octave
    cv_frames = np.interp(tq * SR, np.arange(n), cv)
    fc_frames = 2 ** (a + b * np.clip(cv_frames, 0, 10))
    nf = len(tq)
    xq_all = np.log2(np.maximum(fq, 1.0)[:, None] / fc_frames[None, :])  # octaves from the cutoff

    def granular(base, rng, L, limit, bank, near_cv):
        pool = np.where((tq > FIT[0] + 0.5) & (tq < FIT[1] - 0.5))[0]
        _, _, Zb = spec(base)
        gains = np.zeros(Zb.shape)
        for offset in (0, L // 2):
            for g0 in range(-offset, nf, L):
                k = rng.integers(len(bank))
                centre = min(max(g0 + L // 2, 0), nf - 1)
                cand = pool[np.abs(cv_frames[pool] - cv_frames[centre]) < 1.0] if near_cv else pool
                src = int(rng.choice(cand if len(cand) else pool))
                for i in range(L):
                    t_ = g0 + i
                    if t_ < 0 or t_ >= nf:
                        continue
                    w = 1 - abs((i + 0.5) / L * 2 - 1)  # triangular; the two streams sum to ~1
                    fsrc = fc_frames[src + i] * 2 ** xq_all[:, t_]  # same octaves-from-cutoff in the source frame
                    g = np.clip(bank[k][:, src + i], -limit, limit)
                    gains[:, t_] += w * np.interp(np.log2(np.maximum(fsrc, 1.0)), lc, g)
        return istft(np.abs(Zb) * 10 ** (gains / 20) * np.exp(1j * np.angle(Zb)), SR,
                     nperseg=NFFT, noverlap=NFFT - HOP)[1][:n]

    g1 = lambda base, rng: granular(base, rng, 8, 12.0, raw, False)
    g2 = lambda base, rng: granular(base, rng, 16, 9.0, smooth, True)
    both = lambda base, rng: g2(rp(base, rng), rng)

    # The ceiling: noise shaped frame by frame to the clip's own measured random part.
    rng = np.random.default_rng(1)
    S = [np.abs(stft(t, SR, nperseg=NFFT)[2]) ** 2 for t in resid]
    _, _, Zn = stft(rng.standard_normal(n), SR, nperseg=NFFT)
    oracle = mean + istft(Zn / (np.abs(Zn) + 1e-12) * np.sqrt(np.mean(S, axis=0)), SR, nperseg=NFFT)[1][:n]

    target = {v: np.mean([bands(t, seg) for t in al], axis=0) for v, seg in HOLDS.items()}
    def gap(c):
        return float(np.mean([np.mean(np.abs(bands(c, seg) - target[v])) for v, seg in HOLDS.items()]))

    fixed = {"real1": al[0], "real2": al[1], "mean": mean, "gru": gru, "mean_oracle": oracle}
    scores = {k: [gap(c)] for k, c in fixed.items()}
    clips = dict(fixed)
    for seed in SEEDS:
        for bname, base in (("mean", mean), ("gru", gru)):
            for lname, fn in (("g1", g1), ("g2", g2), ("rp", rp), ("both", both)):
                c = fn(base, np.random.default_rng(seed))
                scores.setdefault(f"{bname}_{lname}", []).append(gap(c))
                if seed == SEEDS[0]:
                    clips[f"{bname}_{lname}"] = c
    gain = 10 ** (-1 / 20) / max(np.abs(c[A:B]).max() for c in clips.values())
    for k, c in clips.items():
        sf.write(OUT / f"res{r}_{k}.wav", np.clip(c[A:B] * gain, -1, 1), SR, subtype="PCM_16")
    report[r] = {"cutoff_fit_from_sweep": f"f = 2^({a:.2f} + {b:.3f} V) Hz",
                 "band_gap_db": {k: {"mean": round(float(np.mean(v)), 2), "seeds": [round(x, 2) for x in v]}
                                 for k, v in scores.items()}}
    print(f"Res {r}: cutoff fit from the sweep {report[r]['cutoff_fit_from_sweep']}")
    for k, v in scores.items():
        print(f"  {k:12s} {np.mean(v):5.2f}" + (f"  (seeds {', '.join(f'{x:.2f}' for x in v)})" if len(v) > 1 else ""))
(OUT / "report.json").write_text(json.dumps(report, indent=1))
