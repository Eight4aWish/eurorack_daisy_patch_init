"""Can the Wasp's randomness at high Res be layered on top of a network? (David, 2026-10-20)

Per Res mark (9, 10), on the `test` take, identical input in four recordings
(lp-res<N> and lp-res<N>-rep1..3), aligned:
  real1, real2   two real takes
  mean           their average: the repeatable part, what a perfect network would play
  gru            the first-recipe cutoff + Res GRU-24 (lp_cr_gru24_s2, Mirth's f1)
  *_gran         David's granular EQ: no added signal. A bank of how takes 2-4 lift and
                 dip against the average, in twelfth-octave bands frame by frame, filed by
                 octaves from the cutoff; ~85 ms grains drawn from it at random, applied as
                 gains to the spectrum underneath, its phase kept
  *_noise        a random-phase copy of the output, scaled per bin by how random the Wasp
                 is at that distance from the cutoff
  mean_oracle    mean + noise shaped frame by frame to the measured random part: the most
                 a layer could give (it uses the takes it is scored against)
Writes 16-bit WAVs of 8-24 s at one gain per mark to /tmp/noise_layer, a report.json, and
prints each clip's band gap: mean |dB| between its octave-band levels and the four takes'
average at the 4-10 V holds. Run on the machine with the captures:
  .venv-nam/bin/python tools/wasp_noise_layer.py"""
import json, pathlib, sys, copy
import numpy as np, soundfile as sf, torch
from scipy.signal import welch, stft, istft

DN = pathlib.Path.home() / "GitHub/eurorack_daisy_patch_init/daisy_neural"
sys.path.insert(0, str(DN / "tools")); import train_gru as T
W = DN / "captures" / "wasp"; OUT = pathlib.Path("/tmp/noise_layer"); OUT.mkdir(exist_ok=True)
SR = 48000; A, B = 8 * SR, 24 * SR
HOLDS = {2: (6.3, 7.9), 4: (10.3, 11.9), 6: (14.3, 15.9), 8: (18.3, 19.9), 10: (22.3, 23.9)}
BANDS = ((50, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000))
rng = np.random.default_rng(124)


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


m = T.RNNModel(3, 24); m.load_state_dict(torch.load(W / "gru_runs/lp_cr_gru24_s2/best.pt", map_location="cpu")); m.eval()
report = {}
for r in (9, 10):
    names = [f"lp-res{r}-rep1", f"lp-res{r}-rep2", f"lp-res{r}-rep3", f"lp-res{r}"]
    takes = [np.load(W / s / "gru" / "test.npz")["y_lphp"].astype(np.float64) for s in names]
    al = [takes[0]] + [align(takes[0], t) for t in takes[1:]]
    n = min(map(len, al)); al = [t[:n] for t in al]
    mean = np.mean(al, axis=0); resid = [t - mean for t in al]
    z = np.load(W / names[0] / "gru" / "test.npz"); cv = z["cv_v"][:n].astype(np.float64)
    take = T.Take(W / names[0] / "gru" / "test.npz"); take.res = float(r)
    _, o = T.evaluate(m, [(take, 0, take.n, T.SETTLE, False)], ["audio", "cv", "res"], "lphp", "cpu", keep=True)
    gru = o[0][:n].astype(np.float64)

    # Where the random part peaks at each hold, and its level against the whole signal.
    peak_hz, level_db = {}, {}
    for v, seg in HOLDS.items():
        f = psd(resid[0], seg)[0]
        prs = np.mean([psd(t, seg)[1] for t in resid], axis=0)
        sm = np.convolve(prs, np.ones(5) / 5, mode="same")
        band = (f > 40) & (f < 16000)
        peak_hz[v] = float(f[band][np.argmax(sm[band])])
        e_r = np.mean([np.mean(t[int(seg[0]*SR):int(seg[1]*SR)] ** 2) for t in resid])
        e_s = np.mean([np.mean(t[int(seg[0]*SR):int(seg[1]*SR)] ** 2) for t in al])
        level_db[v] = float(10 * np.log10(e_r / e_s))
    vs = np.array([v for v in HOLDS if v >= 4]); hz = np.array([peak_hz[v] for v in vs])
    b, a = np.polyfit(vs, np.log2(hz), 1)  # log2 f = a + b V
    fc = 2 ** (a + b * np.clip(cv, 0, 10))
    lv = np.interp(np.clip(cv, 2, 10), list(HOLDS), [level_db[v] for v in HOLDS])

    # Practical layer (v2): a random-phase copy of the GRU's own output, scaled per bin by a
    # table of how random the Wasp is at that frequency relative to the cutoff. The table:
    # for each hold (4-10 V) and band, rho = random / whole energy, against x = log2(band
    # centre / cutoff); interpolated in x. The GRU stands for the repeatable part, so the
    # layer's power is the GRU's times rho / (1 - rho).
    xs_, rho_ = [], []
    for v, seg in HOLDS.items():
        if v < 4:
            continue
        f_c = 2 ** (a + b * v)
        er = np.mean([10 ** (bands(t, seg) / 10) for t in resid], axis=0)
        es = np.mean([10 ** (bands(t, seg) / 10) for t in al], axis=0)
        for (lo, hi), e1, e2 in zip(BANDS, er, es):
            xs_.append(np.log2(np.sqrt(lo * hi) / f_c)); rho_.append(min(0.95, e1 / e2))
    order = np.argsort(xs_); xs_ = np.array(xs_)[order]; rho_ = np.array(rho_)[order]
    # smooth the scatter into a curve: running median over 5 neighbours
    rho_s = np.array([np.median(rho_[max(0, i - 2):i + 3]) for i in range(len(rho_))])
    nper = 2048
    fr, tt, G = stft(gru, SR, nperseg=nper)
    cv_t = np.interp(tt * SR, np.arange(n), cv)
    fc_t = 2 ** (a + b * np.clip(cv_t, 0, 10))
    X = np.log2(np.maximum(fr[:, None], 1.0) / fc_t[None, :])
    rho_grid = np.interp(X, xs_, rho_s, left=rho_s[0], right=rho_s[-1])
    gain_grid = np.sqrt(rho_grid / (1 - rho_grid))
    def layer_from(base):
        _, _, Gb = stft(base, SR, nperseg=nper)
        ph = np.exp(2j * np.pi * rng.random(Gb.shape))
        return istft(np.abs(Gb) * gain_grid * ph, SR, nperseg=nper)[1][:n]
    gru_noise = gru + layer_from(gru)
    mean_noise = mean + layer_from(mean)

    # David's granular EQ (2026-10-20): no added signal. The bank is how each real take's
    # spectrum lifts and dips against the average, per bin and frame, stored against
    # octaves from that frame's cutoff (takes 2-4 only, so take 1 stays an independent
    # reference). Grains of 8 frames (~85 ms) are drawn at random from it, two staggered
    # streams crossfaded, re-mapped to the target's cutoff and applied as gains to the
    # target's spectrum; the target's own phase is kept.
    hop, nfft = 512, 2048
    def spec(y):
        f_, t_, Z_ = stft(y, SR, nperseg=nfft, noverlap=nfft - hop)
        return f_, t_, Z_
    fq, tq, Zm = spec(mean)
    # Gains measured in twelfth-octave bands (40 Hz - 20 kHz), not single bins: a near-empty
    # bin makes the ratio of two tiny numbers swing wildly (the first version did, measured
    # 2026-10-20: worse than no layer). Limited to +-12 dB.
    edges = 40 * 2 ** (np.arange(0, 9 * 12 + 1) / 12); edges = edges[edges < 20000]
    centres = np.sqrt(edges[:-1] * edges[1:])
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
    bank = []
    cv_frames = np.interp(tq * SR, np.arange(n), cv)
    fc_frames = 2 ** (a + b * np.clip(cv_frames, 0, 10))
    for t_ in al[1:]:
        _, _, Zk = spec(t_)
        bank.append(np.clip(10 * np.log10(band_power(Zk) / Pm), -12, 12))
    live = np.where((tq > 6) & (tq < 24))[0]  # frames with the riff playing
    L = 8
    def granular(base):
        _, _, Zb = spec(base)
        nf = Zb.shape[1]
        gains = np.zeros(Zb.shape)
        for stream, offset in ((0, 0), (1, L // 2)):
            for g0 in range(-offset, nf, L):
                k = rng.integers(len(bank)); src = int(rng.choice(live[:-L]))
                for i in range(L):
                    t_ = g0 + i
                    if t_ < 0 or t_ >= nf:
                        continue
                    w = 1 - abs((i + 0.5) / L * 2 - 1)  # triangular; the two streams sum to ~1
                    xq = np.log2(np.maximum(fq, 1.0) / fc_frames[min(t_, nf - 1)])
                    fsrc = fc_frames[src + i] * 2 ** xq  # same octaves-from-cutoff in the bank frame
                    gains[:, t_] += w * np.interp(np.log2(np.maximum(fsrc, 1.0)), np.log2(centres), bank[k][:, src + i])
        return istft(np.abs(Zb) * 10 ** (gains / 20) * np.exp(1j * np.angle(Zb)), SR,
                     nperseg=nfft, noverlap=nfft - hop)[1][:n]
    gru_gran = granular(gru)
    mean_gran = granular(mean)
    table = {f"{x:+.1f}": round(float(10 * np.log10(r_)), 1) for x, r_ in zip(xs_[::3], rho_s[::3])}

    # Oracle layer: noise shaped to the measured random part's spectrum, frame by frame.
    nper = 2048
    S = [np.abs(stft(t, SR, nperseg=nper)[2]) ** 2 for t in resid]
    env = np.sqrt(np.mean(S, axis=0))
    _, _, Zn = stft(rng.standard_normal(n), SR, nperseg=nper)
    Zn = Zn / (np.abs(Zn) + 1e-12) * env
    oracle = istft(Zn, SR, nperseg=nper)[1][:n]
    mean_oracle = mean + oracle

    clips = {"real1": al[0], "real2": al[1], "mean": mean, "mean_gran": mean_gran, "mean_noise": mean_noise,
             "mean_oracle": mean_oracle, "gru": gru, "gru_gran": gru_gran, "gru_noise": gru_noise}
    gain = 10 ** (-1 / 20) / max(np.abs(c[A:B]).max() for c in clips.values())
    for k, c in clips.items():
        sf.write(OUT / f"res{r}_{k}.wav", np.clip(c[A:B] * gain, -1, 1), SR, subtype="PCM_16")
    # Band-level distance from the real takes, holds 4-10 V (mean |dB|), and take 2 against take 1 for scale.
    def dist(c):
        return float(np.mean([np.mean(np.abs(bands(c, seg) - np.mean([bands(t, seg) for t in al], axis=0)))
                              for v, seg in HOLDS.items() if v >= 4]))
    report[r] = {"cutoff_fit": f"f = 2^({a:.2f} + {b:.3f} V) Hz", "peak_hz": peak_hz, "rho_db_by_octaves_from_cutoff": table,
                 "random_level_db": {v: round(x, 1) for v, x in level_db.items()},
                 "band_dist_db": {k: round(dist(c), 2) for k, c in clips.items()}}
    print(f"Res {r}: random part peaks at " + ", ".join(f"{v} V {peak_hz[v]:.0f} Hz" for v in HOLDS)
          + f"; fit {report[r]['cutoff_fit']}")
    print("  randomness (dB of the whole) by octaves from the cutoff: " + ", ".join(f"{k}: {v}" for k, v in table.items()))
    print("  band-level distance from the real takes (dB, holds 4-10 V): "
          + ", ".join(f"{k} {v}" for k, v in report[r]["band_dist_db"].items()))
(OUT / "report.json").write_text(json.dumps(report, indent=1))
