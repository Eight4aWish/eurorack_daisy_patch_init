#!/usr/bin/env python3
"""
notamp_options.py — more not-amps to audition, chosen for diversity (David, 2026-10-25:
"great to represent more diversity and great to audition some more options").

The search (tools/notamp_search.py) kept the routes that leave the amps; the card's
sixteen were picked from them farthest-first, then by ear. This asks a different
question of the same measurements: which routes are furthest from the sixteen already
on the card? Every candidate that passed both of the search's judges (candidates.csv,
ok) is ranked by its distance to the nearest not-amp on the card, in amp spacings
(Space, the search's own measure, between representative points: the mean of each
route's outside points, as score() takes them), and picked greedily, farthest first,
at most two per family of change (activation, feedback, …),
one per transform and two per capture: distance alone fills the list with feedback, which
lands far from everything but is one kind of change.

Each pick must also pass the noise screen notamp_design.py applies before anything goes
on the card (faint noise in, -70 dBFS, must not come out within 10 dB of playing level
as noise), and must not be a route the bench already turned down: FB PCH KAY and
FB PCH PLX grated, SINE PLX amplified noise.

Every pick, and the card's not-amps the map puts closest together (for comparison),
is rendered through the search's harness (tools/a2_explore, which the firmware matches
bit for bit) on the riff four times over, 16 s, the steer swept from one end to the
other; then the firmware's 20 Hz DC blocker, and a slow level correction standing in
for the module's level table. Output, gitignored: amp_compare/notamp_options/ — a WAV
and an MP3 per clip, and options.json for the listening page.

    .venv-nam/bin/python tools/notamp_options.py [--picks 8]
"""
import argparse
import csv
import json
import math
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import notamp_map as M  # noqa: E402
import notamp_search as S  # noqa: E402

PROJECT = HERE.parent
OUT = PROJECT / "amp_compare" / "notamp_options"
SR = 48000
BENCH_NO = {("KAY 703", "feedback pitch"), ("PLEXI LORE", "feedback pitch"), ("PLEXI LORE", "sine neurons")}
COMPARE = ["FRZ E TR", "FRZ M F57", "FRZ M KAY", "RECT ORG", "RATE SVT"]

SHORT_T = {"sine neurons": "SINE", "tanh neurons": "TANH", "slope→linear": "LINEAR", "slope→rectify": "RECT",
           "feedback 500 Hz": "FB500", "feedback 100 Hz": "FB100", "feedback pitch": "FB PCH", "rate ÷": "RATE",
           "gap stretch": "STRETCH", "mutate": "MUTATE", "freeze mid": "FRZ M", "freeze early": "FRZ E",
           "fold inside": "FOLD", "past a partner": "PAST", "bias off": "BIAS 0", "bias ×4": "BIAS X4",
           "mix-in off": "MIX 0", "mix-in ×6": "MIX X6", "rectified mix-in": "MIX RECT",
           "delayed mix-in": "MIX DLY", "lane offset": "OFFSET", "long gaps out": "NO LONG"}
SHORT_C = {"SVT-2 PRO": "SVT", "ORANGE TH": "ORG", "FENDER 57": "F57", "BLUESBRKR": "BLU", "KLON": "KLON",
           "PLEXI LORE": "PLX", "SUNBENDER": "SUN", "TWO ROCK": "TR", "KAY 703": "KAY", "TRAYNOR": "TRY",
           "BUGERA G5": "BUG", "DIY BJT": "BJT"}
SAYS = {"sine neurons": "every neuron's activation a sine, sin(g·x)/g",
        "tanh neurons": "every neuron's activation a soft clip, tanh(g·x)/g",
        "slope→linear": "every neuron's activation straightened",
        "slope→rectify": "every neuron's activation flipped below zero: a rectifier",
        "feedback 500 Hz": "the output fed back into the input 96 samples later (500 Hz)",
        "feedback 100 Hz": "the output fed back into the input 480 samples later (100 Hz)",
        "feedback pitch": "the output fed back, the delay swept: the loop's pitch",
        "rate ÷": "the engine run at a half, a third … a sixth of the sample rate",
        "gap stretch": "every gap stretched, ×1 to ×4: the network reads its history at the wrong spacing",
        "mutate": "a fixed random direction added to every weight",
        "freeze mid": "layer 12's output held", "freeze early": "layer 4's output held",
        "fold inside": "a triangle fold inside layer 12", "past a partner": "every weight pushed past it, away from a partner",
        "bias off": "every bias faded to zero", "bias ×4": "every bias scaled up to ×4",
        "mix-in off": "the raw sample each layer adds, faded out", "mix-in ×6": "the raw sample each layer adds, up to ×6",
        "rectified mix-in": "the raw sample each layer adds, rectified", "delayed mix-in": "the raw sample each layer adds, delayed up to 20 ms",
        "lane offset": "an offset on one lane of layer 4", "long gaps out": "the three long-gap layers (7, 14 and 23) faded out"}


def sweep_spec(t):
    """The a2_explore spec sweeping t's control across the clip, or a list of fixed specs
    (one per pass) for the stepped ones."""
    name, fam, op, lo, hi, scale = t[:6]
    if scale == "step":
        return [f"{op}:{v}" for v in lo[1:]]          # rate: 2, 3, 4, 6, one per pass
    if scale == "stepbin":
        return [("none", s) for s in lo[:4]]           # stretch: x1..x4, one build each
    if op.startswith(("fbgain", "freeze", "fold", "off", "blend")) or op.count(":"):
        return f"{op}:{lo:g}:{hi:g}"
    return f"{op}:{lo:g}:{hi:g}"


def run_explore(wA, wB, sig, spec, S_=1):
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        wA.astype("<f4").tofile(d / "a.f32")
        if wB is not None:
            wB.astype("<f4").tofile(d / "b.f32")
        sig.astype("<f4").tofile(d / "in.f32")
        r = subprocess.run([str(HERE / f"a2_explore_s{S_}"), str(d / "a.f32"), str(d / "b.f32") if wB is not None else "-",
                            str(d / "in.f32"), str(d / "out.f32"), spec], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{spec}: {r.stderr}")
        return np.fromfile(d / "out.f32", "<f4").astype(np.float64)[: len(sig)]


def dc_block(y):
    R = 1 - 2 * math.pi * 20 / SR
    return lfilter([1, -1], [1, -R], y)


def level(y, target_db=-18.0):
    """A slow level correction, standing in for the module's level table: the gain that
    brings a 1 s moving RMS to the target, smoothed, then the whole clip under -1 dBFS."""
    win = SR
    p = np.convolve(y ** 2, np.ones(win) / win, mode="same")
    g = 10 ** (target_db / 20) / np.sqrt(np.maximum(p, 1e-10))
    g = np.minimum(g, 10 ** (60 / 20))
    g = np.convolve(g, np.ones(SR // 2) / (SR // 2), mode="same")
    out = y * g
    pk = np.max(np.abs(out))
    return out * min(1.0, 0.89 / pk) if pk > 0 else out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--picks", type=int, default=8)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    C = json.loads(M.CACHE.read_text())
    A = S.matrix([C["amps"][a] for a in C["amps"]])
    keys = [k for k, d in C["cands"].items() if d]
    K = S.matrix([C["cands"][k] for k in keys])
    sp = S.Space({g: np.vstack([A[g], K[g]]) for g in S.GROUPS})
    zA, zK = sp.z(A), sp.z(K)
    dAA = S.Space.dist(zA, zA)
    np.fill_diagonal(dAA, np.inf)
    tau = float(np.percentile(dAA.min(1), 95))
    idx = {k: i for i, k in enumerate(keys)}
    dKA = S.Space.dist(zK, zA).min(1) / tau

    def rep_rows(ii, d):
        sel = [j for j, o in zip(ii, d) if o > 1] or ii
        return {g: zK[g][sel].mean(0, keepdims=True) for g in S.GROUPS}

    def route(cap, tr):
        t = next(t for t in S.T if t[0] == tr)
        ii = [idx[f"{cap}|{tr}|{i}"] for i in range(len(S.knob_values(t)))]
        return t, ii, dKA[ii]

    card = {}
    for n, (cap, tr) in M.CARD.items():
        _, ii, d = route(cap, tr)
        card[n] = rep_rows(ii, d)
    rnd = M.measure_rnd()
    zR = sp.z(S.matrix([d for d, _ in rnd.values()]))
    rnames = list(rnd)
    dR = S.Space.dist(zR, zA).min(1) / tau
    for name in M.RND:
        ii = [rnames.index(f"{name}|{i}") for i in range(len(S.U))]
        sel = [j for j in ii if dR[j] > 1] or ii
        card[name] = {g: zR[g][sel].mean(0, keepdims=True) for g in S.GROUPS}

    used = set(M.CARD.values())
    pool = []
    for r in csv.DictReader(open(PROJECT / "amp_compare" / "notamp_search" / "candidates.csv")):
        key = (r["capture"], r["transform"])
        if r["ok"] != "True" or key in used or key in BENCH_NO:
            continue
        t, ii, d = route(*key)
        pool.append(dict(capture=key[0], transform=key[1], family=t[1], t=t, rep=rep_rows(ii, d),
                         novelty=float(np.median(d)), outside=float(np.mean(d > 1))))
    print(f"amp spacing {tau:.3f}; {len(pool)} candidates passed both judges and are not on the card")

    # the noise screen, as notamp_design.py runs it before anything goes on the card
    dry = np.asarray(S.make_synth(), np.float64)
    S.scale_peak(dry, 0.5)
    rng = np.random.default_rng(0)
    idle = rng.standard_normal(len(dry)) * 10 ** (-70 / 20)
    starter = dict(S.a2nb_weights(f) for f in sorted(S.STARTER.glob("n*.a2nb")))
    nrng = np.random.default_rng(7)               # the search's fixed mutation directions
    noise = {n: (nrng.standard_normal(1871) * np.std(starter[n])).astype("<f4") for n in starter}

    def weights(c):
        if c["t"][7] == "partner":
            return starter[C["partner"][c["capture"]]], starter[c["capture"]]
        return starter[c["capture"]], (noise[c["capture"]] if c["t"][7] == "noise" else None)

    def noisy(c):
        wA, wB = weights(c)
        for v in S.knob_values(c["t"]):
            spec, S_ = S.spec_for(c["t"], v)
            play = dc_block(run_explore(wA, wB, dry, spec, S_ or 1))[9600:]
            quiet = dc_block(run_explore(wA, wB, idle, spec, S_ or 1))[9600:]
            gap = 20 * np.log10(np.sqrt(np.mean(quiet ** 2)) / (np.sqrt(np.mean(play ** 2)) + 1e-30) + 1e-30)
            spec_ = np.abs(np.fft.rfft(quiet * np.hanning(len(quiet)))) ** 2 + 1e-30
            flat = float(np.exp(np.mean(np.log(spec_))) / np.mean(spec_))
            if gap > -10 and flat > 0.1:
                return f"{v:g}: idle {gap:+.1f} dB, flatness {flat:.2f}"
        return None

    chosen, reps = [], dict(card)
    per_f, per_t, per_c = {}, {}, {}
    while len(chosen) < args.picks and pool:
        for c in pool:
            dist = {n: S.Space.dist(c["rep"], r)[0, 0] / tau for n, r in reps.items()}
            c["nearest"] = min(dist, key=dist.get)
            c["diversity"] = dist[c["nearest"]]
        pool.sort(key=lambda c: -c["diversity"])
        for c in pool:
            if per_f.get(c["family"], 0) >= 2 or per_t.get(c["transform"], 0) >= 1 or per_c.get(c["capture"], 0) >= 2:
                continue
            why = noisy(c)
            pool.remove(c)
            if why:
                print(f"  noise screen: {c['capture']} | {c['transform']} fails ({why})")
                break
            c["name"] = f"{SHORT_T[c['transform']]} {SHORT_C[c['capture']]}"
            chosen.append(c)
            reps[c["name"]] = c["rep"]
            per_f[c["family"]] = per_f.get(c["family"], 0) + 1
            per_t[c["transform"]] = per_t.get(c["transform"], 0) + 1
            per_c[c["capture"]] = per_c.get(c["capture"], 0) + 1
            print(f"  pick {c['name']:12s} {c['diversity']:4.1f} spacings from {c['nearest']:11s} "
                  f"{c['novelty']:4.1f} from the amps, outside {c['outside']:.0%}")
            break
        else:
            break

    # the clips: the riff four times, the steer swept end to end
    sig4 = np.tile(dry, 4)
    credits = {}
    for line in (PROJECT / "STARTER_CAPTURES.md").read_text().split("\n"):
        f = [x.strip() for x in line.split("|")]
        if len(f) > 5 and f[1].startswith("`"):
            credits[f[1].strip("`")] = dict(creator=f[4].split("]")[0].lstrip("["), licence=f[5].split("]")[0].lstrip("["))

    def render_clip(c):
        wA, wB = weights(c)
        spec = sweep_spec(c["t"])
        if isinstance(spec, list):               # stepped: one value per pass of the riff
            parts = []
            for s in spec:
                sp_, S_ = (s, 1) if isinstance(s, str) else s
                parts.append(run_explore(wA, wB, dry, sp_, S_))
            y = np.concatenate(parts)
        else:
            y = run_explore(wA, wB, sig4, spec)
        return level(dc_block(y))

    clips = []
    for c in chosen + [dict(name=n, capture=M.CARD[n][0], transform=M.CARD[n][1],
                            t=next(t for t in S.T if t[0] == M.CARD[n][1]), on_card=True) for n in COMPARE]:
        if c.get("on_card"):
            _, ii, d = route(c["capture"], c["transform"])
            c["novelty"], c["outside"] = float(np.median(d)), float(np.mean(d > 1))
            dist = {n: S.Space.dist(card[c["name"]], r)[0, 0] / tau for n, r in card.items() if n != c["name"]}
            c["nearest"] = min(dist, key=dist.get)
            c["diversity"] = dist[c["nearest"]]
        y = render_clip(c)
        fid = c["name"].replace(" ", "_")
        sf.write(OUT / f"{fid}.wav", y.astype(np.float32), SR, subtype="FLOAT")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(OUT / f"{fid}.wav"), "-codec:a", "libmp3lame",
                        "-b:a", "192k", str(OUT / f"{fid}.mp3")], check=True)
        env = np.abs(y)
        k = len(env) // 400
        t = c["t"]
        vals = S.knob_values(t)
        clips.append(dict(id=fid, name=c["name"], capture=c["capture"], transform=c["transform"], family=t[1],
                          says=SAYS[c["transform"]], steer=[round(float(v), 4) for v in vals], law=t[5], cpu=t[8],
                          partner=C["partner"][c["capture"]] if t[7] == "partner" else None,
                          novelty=round(c["novelty"], 2), outside=round(c["outside"], 2),
                          diversity=round(c["diversity"], 2), nearest=c["nearest"], on_card=bool(c.get("on_card")),
                          seconds=round(len(y) / SR, 2), peaks=[round(float(v), 3) for v in env[: k * 400].reshape(400, k).max(1)],
                          **credits.get(c["capture"], {})))
    (OUT / "options.json").write_text(json.dumps(dict(tau=round(tau, 4), clips=clips), ensure_ascii=False))
    print(f"wrote {len(clips)} clips to {OUT}")


if __name__ == "__main__":
    main()
