"""David's hunch: the input's pitches pull the self-oscillation around. Per hold of the sw-a
test takes, in 170 ms frames: the oscillation's pitch (the strongest output peak within
four semitones of where Swords oscillates with no input), and whether it sits on one of
the input's partials more often than its home frequency would by chance. Then, for the
two lower holds: does the pitch lean towards the strongest nearby input partial, or follow
the input's level, and do the three takes trace the same pitch track?

    .venv/bin/python tools/swords_pull.py
"""
import json, pathlib
import numpy as np

D = pathlib.Path(__file__).resolve().parents[1] / "captures/swords"
SR, N, HOP, PAD = 48000, 8192, 2048, 4
# Home: where it oscillates with no input (step 0: 168.9 Hz at +4 V, 1274.8 Hz at +7 V).
slope = np.log2(1274.8 / 168.9) / 3
home = lambda v: 168.9 * 2 ** (slope * (v - 4))  # noqa: E731
s = json.loads((D / "sw-a/session.json").read_text())
holds = s["test_holds_v"]
win = np.hanning(N)
f = np.fft.rfftfreq(N * PAD, 1 / SR)


def peaks(mag, band, floor_db):
    """Local maxima inside band within floor_db of the band's loudest, Hz (interpolated) and dB."""
    i = np.where(band)[0]
    m = 20 * np.log10(mag[i] + 1e-30)
    top = m.max()
    out = []
    for j in range(1, len(i) - 1):
        if m[j] > m[j - 1] and m[j] >= m[j + 1] and m[j] > top - floor_db:
            a, b, c = m[j - 1], m[j], m[j + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c)
            out.append(((i[j] + d) * SR / (N * PAD), b))
    return out


def cents(a, b):
    return 1200 * np.log2(a / b)


print("hold V  home Hz | Res  osc pitch over the hold, cents from home: median, p5..p95 | "
      "frames on an input partial (within 15 cents): osc / home / chance")
for i, hv in enumerate(holds):
    if hv < 4:  # under ~180 Hz a 170 ms frame cannot place a pitch to 15 cents
        continue
    fh = home(hv)
    band = (f > fh * 2 ** (-4 / 12)) & (f < fh * 2 ** (4 / 12))
    for r in (3.5, 4.0, 4.5, 5.5):
        z = np.load(D / f"sw-a/gru/test_r{r:+.2f}_1.npz")
        x, y = z["x"].astype(np.float64), z["y_f1"].astype(np.float64)
        a0, a1 = int((4 * i + 0.5) * SR), int((4 * i + 3.9) * SR)
        dev, on_osc, on_home, chance = [], 0, 0, 0
        n = 0
        for t in range(a0, a1 - N, HOP):
            Y = np.abs(np.fft.rfft(y[t:t + N] * win, N * PAD))
            X = np.abs(np.fft.rfft(x[t:t + N] * win, N * PAD))
            py = peaks(Y, band, 60)
            px = peaks(X, band, 40)
            if not py:
                continue
            fo = max(py, key=lambda p: p[1])[0]
            dev.append(cents(fo, fh))
            n += 1
            if px:
                xs = np.array([p[0] for p in px])
                on_osc += np.min(np.abs(cents(xs, fo))) < 15
                on_home += np.min(np.abs(cents(xs, fh))) < 15
                # chance: a frequency drawn uniformly (in cents) across the band
                g = fh * 2 ** (np.linspace(-4, 4, 97) / 12)
                chance += np.mean([np.min(np.abs(cents(xs, gg))) < 15 for gg in g])
        dev = np.array(dev)
        print(f"{hv:+5.1f}  {fh:7.0f} | {r:3.1f}  {np.median(dev):+6.0f}  {np.percentile(dev, 5):+6.0f}..{np.percentile(dev, 95):+5.0f}"
              f"   | {on_osc / n:4.0%} / {on_home / n:4.0%} / {chance / n:4.0%}   ({n} frames)")
    print()

print("hold  Res | pitch v the strongest nearby input partial (signed cents): r, slope | pitch v input level: r, cents/dB | takes agree on the pitch track: rms cents")
for i, hv in enumerate(holds):
    if hv < 4 or hv > 6:
        continue
    fh = home(hv)
    band = (f > fh * 2 ** (-4 / 12)) & (f < fh * 2 ** (4 / 12))
    for r in (4.5, 5.5):
        tracks = []
        for k in (1, 2, 3):
            z = np.load(D / f"sw-a/gru/test_r{r:+.2f}_{k}.npz")
            x, y = z["x"].astype(np.float64), z["y_f1"].astype(np.float64)
            a0, a1 = int((4 * i + 0.5) * SR), int((4 * i + 3.9) * SR)
            dev, near, lvl = [], [], []
            for t in range(a0, a1 - N, HOP):
                Y = np.abs(np.fft.rfft(y[t:t + N] * win, N * PAD))
                X = np.abs(np.fft.rfft(x[t:t + N] * win, N * PAD))
                py, px = peaks(Y, band, 60), peaks(X, band, 40)
                if not py or not px:
                    dev.append(np.nan); near.append(np.nan); lvl.append(np.nan); continue
                dev.append(cents(max(py, key=lambda p: p[1])[0], fh))
                near.append(cents(max(px, key=lambda p: p[1])[0], fh))
                lvl.append(10 * np.log10(np.mean(x[t:t + N] ** 2) + 1e-30))
            tracks.append(np.array(dev))
            if k == 1:
                d, nn, l = np.array(dev), np.array(near), np.array(lvl)
        m = np.isfinite(d) & np.isfinite(nn) & np.isfinite(l)
        rn = np.corrcoef(d[m], nn[m])[0, 1]; sn = np.polyfit(nn[m], d[m], 1)[0]
        rl = np.corrcoef(d[m], l[m])[0, 1]; sl = np.polyfit(l[m], d[m], 1)[0]
        T = np.array(tracks); agree = np.sqrt(np.nanmean((T - np.nanmean(T, axis=0)) ** 2))
        print(f"{hv:+4.1f} {r:3.1f} |  r {rn:+.2f}, {sn:+.3f} cents per cent  |  r {rl:+.2f}, {sl:+5.1f} c/dB  |  {agree:4.1f}  (spread {np.nanstd(d):.0f})")
