#!/usr/bin/env python3
"""
notamp_search.py — find not-amps that do not sound like a synth through a guitar amp.

The first nine not-amps were chosen by ear from bends of five captures. This one
measures. It maps where real guitar-amp captures sit in a space of audio
descriptors, renders every transform on the menu (tools/a2_explore.cpp) applied to
each starter capture at five settings of its one control, and keeps the transforms
that (1) leave the amp region and stay out of it across most of the knob, (2) steer
smoothly, (3) keep a sane level — then picks the most mutually different of those.

Descriptors, four equal-weight groups, from librosa plus what the test phrase lets
us know exactly (its notes are known, so harmonics can be counted, not guessed):
    timbre     MFCC 1-19 means, spectral contrast, flatness, centroid, rolloff
    harmonics  energy on the notes' harmonics / between them / below the root;
               odd vs even harmonic balance
    dynamics   level and brightness change from a soft to a hot input
    time       sustain into the rests, how well the output follows the input's
               envelope, frame-to-frame spectral flux variation
Each descriptor is robust-scaled across everything rendered (median / IQR, clipped
at 6), so no one number dominates its group.

"Outside the amp region" means: further from every real capture than 95% of real
captures are from their own nearest neighbour.

Usage (in the project .venv):
    .venv/bin/python tools/notamp_search.py              # renders, scores, selects
    .venv/bin/python tools/notamp_search.py --reuse      # rescore from the cache
Outputs go to amp_compare/notamp_search/ (gitignored): report.md, candidates.csv,
map.png, audition.wav and its index.
"""

import argparse
import concurrent.futures as cf
import csv
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import uuid
import zipfile

import librosa
import numpy as np
import soundfile as sf

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))
from quantisation_study import make_synth, scale_peak  # noqa: E402

SR = 48000
OUT = PROJECT / "amp_compare" / "notamp_search"
STARTER = PROJECT / "captures" / "starter"
DOWNLOADS = pathlib.Path.home() / "Library/Mobile Documents/com~apple~CloudDocs/Downloads"

# The test phrase, as make_synth() builds it: eight notes of 0.5 s, sounding 0.42 s.
ROOT, NOTE_LEN, GATE = 110.0, 0.5, 0.42
SEQ = [0, 0, 3, 5, 0, -2, 0, 7]
F0 = [ROOT * 2 ** (s / 12) for s in SEQ]
U = [0.0, 0.25, 0.5, 0.75, 1.0]  # knob positions measured

# ---------------------------------------------------------------- transforms
# (name, family, spec template, lo, hi, scale, binary, needs B, firmware note)
# scale: lin / log for the knob law; stepped ones list their values.
T = [
    ("slope→rectify", "activation", "slope", 0.01, -1.0, "lin", 1, None, "cheap: one constant becomes a variable"),
    ("slope→linear", "activation", "slope", 0.01, 1.0, "lin", 1, None, "cheap"),
    ("sine neurons", "activation", "sine", 0.3, 8.0, "log", 1, None, "a sine per neuron: ~10-15% CPU, to be measured"),
    ("tanh neurons", "activation", "tanh", 0.3, 12.0, "log", 1, None, "a tanh per neuron: ~10-15% CPU, to be measured"),
    ("bias off", "bias", "bias", 1.0, 0.0, "lin", 1, None, "weights rewrite, as MORPH"),
    ("bias ×4", "bias", "bias", 1.0, 4.0, "lin", 1, None, "weights rewrite"),
    ("mix-in off", "mixin", "mixin", 1.0, 0.0, "lin", 1, None, "weights rewrite"),
    ("mix-in ×6", "mixin", "mixin", 1.0, 6.0, "lin", 1, None, "weights rewrite"),
    ("rectified mix-in", "mixin", "condrect", 0.0, 2.0, "lin", 1, None, "cheap"),
    ("delayed mix-in", "mixin", "conddelay", 1.0, 960.0, "log", 1, None, "a delay line"),
    ("feedback 500 Hz", "feedback", "fbgain:96", 0.0, 0.95, "lin", 1, None, "a delay line, ≥ 1 block"),
    ("feedback 100 Hz", "feedback", "fbgain:480", 0.0, 0.95, "lin", 1, None, "a delay line"),
    ("feedback pitch", "feedback", "fbdelay:0.8", 48.0, 1200.0, "log", 1, None, "a delay line"),
    ("rate ÷", "rate", "rate", [1, 2, 3, 4, 6], None, "step", 1, None, "saves CPU; aliases"),
    ("gap stretch", "stretch", "none", [1, 2, 3, 4], None, "stepbin", None, None, "history ×S: ×2 fits RAM_D2, ×4 needs SDRAM"),
    ("mutate", "weights", "mutate", 0.0, 0.5, "lin", 1, "noise", "weights rewrite"),
    ("freeze mid", "bend", "freeze:11", 1.0, 1024.0, "log", 1, None, "exists (FREEZE)"),
    ("freeze early", "bend", "freeze:3", 1.0, 1024.0, "log", 1, None, "exists (FREEZE ERL)"),
    ("fold inside", "bend", "fold:11", 1.0, 0.05, "log", 1, None, "exists (FOLDED)"),
    ("lane offset", "bend", "off:3:0", -2.0, 2.0, "lin", 1, None, "exists (OFFSET)"),
    ("long gaps out", "bend", "blend:6+13+22", 1.0, 0.0, "lin", 1, None, "exists (NO LONG)"),
    ("past a partner", "weights", "morph", 1.0, 1.3, "lin", 1, "partner", "exists (PAST)"),
]


def knob_values(t):
    name, fam, op, lo, hi, scale = t[:6]
    if scale in ("step", "stepbin"):
        return list(lo)
    if scale == "log":
        return [math.exp(math.log(lo) + (math.log(hi) - math.log(lo)) * u) for u in U]
    return [lo + (hi - lo) * u for u in U]


def spec_for(t, v):
    op, scale = t[2], t[5]
    if scale == "stepbin":
        return "none", int(v)
    if scale == "step":
        return f"{op}:{int(v)}", 1
    return f"{op}:{v:.6g}:{v:.6g}", 1


# ---------------------------------------------------------------- captures
def a2nb_weights(path):
    b = path.read_bytes()
    return b[16:28].split(b"\0")[0].decode(), np.frombuffer(b[32:], "<f4").copy()


def nam_lite(blob):
    d = json.loads(blob)
    if d.get("architecture") != "SlimmableContainer":
        return None
    s = min(d["config"]["submodels"], key=lambda s: abs(s["max_value"] - 0.5))
    w = np.asarray(s["model"]["weights"], "<f4")
    return w if len(w) == 1871 else None


def real_amps():
    """Every real A2 capture on this machine: the downloaded packs and the five
    development captures. Licence does not matter here — they are only measured."""
    amps = {}
    for z in sorted(DOWNLOADS.glob("*.zip")):
        try:
            zf = zipfile.ZipFile(z)
        except zipfile.BadZipFile:
            continue
        for m in zf.namelist():
            if m.endswith(".nam") and not m.startswith("__MACOSX"):
                w = nam_lite(zf.read(m))
                if w is not None:
                    amps[f"{z.stem} / {pathlib.Path(m).stem}"] = w
    for f in sorted((PROJECT / "amp_compare").glob("w_kWeights*.f32")):
        amps[f"dev / {f.stem[10:]}"] = np.fromfile(f, "<f4")
    return amps


# ---------------------------------------------------------------- rendering
SIGNAL = None  # the dry test phrase; set in main(), passed to workers as a file


def render(job):
    """job = (wA, wB or None, spec, binary S, level, tmp dir) -> output array.
    Runs in a worker process, so everything it needs comes in the job."""
    wA, wB, spec, S, level, tmp = job
    tmp = pathlib.Path(tmp)
    key = uuid.uuid4().hex
    fa, fb, fi, fo = (tmp / f"{key}_{x}.f32" for x in "abio")
    wA.tofile(fa)
    if wB is not None:
        wB.tofile(fb)
    dry = np.fromfile(tmp / "dry.f32", "<f4")
    (dry * level).astype("<f4").tofile(fi)
    r = subprocess.run([str(HERE / f"a2_explore_s{S}"), str(fa), str(fb) if wB is not None else "-",
                        str(fi), str(fo), spec], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"{spec}: {r.stderr}")
    y = np.fromfile(fo, "<f4").astype(np.float64)[: len(dry)]
    for f in (fa, fb, fi, fo):
        f.unlink(missing_ok=True)
    return y


# ---------------------------------------------------------------- descriptors
def note_spectra(y):
    """Mean magnitude spectrum of each note's sustained part, with its f0."""
    n_fft = 16384
    out = []
    for i, f0 in enumerate(F0):
        a = int((i * NOTE_LEN + 0.06) * SR)
        b = int((i * NOTE_LEN + GATE - 0.02) * SR)
        seg = y[a:b]
        seg = np.pad(seg, (0, max(0, n_fft - len(seg))))[:n_fft]
        mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        out.append((f0, mag, np.fft.rfftfreq(n_fft, 1 / SR)))
    return out


def harmonic_features(y):
    on = between = below = odd = even = total = 0.0
    for f0, mag, f in note_spectra(y):
        band = (f > 0.4 * f0) & (f < 16000)
        total += mag[band].sum()
        below += mag[(f > 0.4 * f0) & (f < 0.8 * f0)].sum()
        k = f / f0
        near = np.abs(k - np.round(k)) < 0.06
        half = np.abs(k - np.floor(k) - 0.5) < 0.06
        on += mag[band & near & (k >= 0.8)].sum()
        between += mag[band & half & (k >= 1)].sum()
        kk = np.round(k)
        odd += mag[band & near & (kk >= 3) & (kk % 2 == 1)].sum()
        even += mag[band & near & (kk >= 2) & (kk % 2 == 0)].sum()
    t = total + 1e-20
    return [on / t, between / t, below / t, 10 * math.log10((odd + 1e-20) / (even + 1e-20))]


def time_features(y, x):
    hop = 480
    ey = librosa.feature.rms(y=y, frame_length=960, hop_length=hop)[0] + 1e-9
    ex = librosa.feature.rms(y=x, frame_length=960, hop_length=hop)[0] + 1e-9
    n = min(len(ey), len(ex))
    track = float(np.corrcoef(np.log(ey[:n]), np.log(ex[:n]))[0, 1])
    rest, note = [], []
    for i in range(len(F0)):
        rest.append(y[int((i * NOTE_LEN + GATE + 0.02) * SR): int((i + 1) * NOTE_LEN * SR)])
        note.append(y[int((i * NOTE_LEN + 0.06) * SR): int((i * NOTE_LEN + GATE) * SR)])
    rms = lambda s: math.sqrt(float(np.mean(np.concatenate(s) ** 2)) + 1e-20)
    sustain = 20 * math.log10(rms(rest) / rms(note))
    flux = librosa.onset.onset_strength(y=y.astype(np.float32), sr=SR, hop_length=hop)
    return [track, sustain, float(np.std(flux) / (np.mean(flux) + 1e-9))]


def timbre_features(y):
    y32 = y.astype(np.float32)
    S = np.abs(librosa.stft(y32, n_fft=2048, hop_length=512)) + 1e-12
    mf = librosa.feature.mfcc(S=librosa.power_to_db(librosa.feature.melspectrogram(S=S ** 2, sr=SR)), n_mfcc=20)
    con = librosa.feature.spectral_contrast(S=S, sr=SR)
    flat = librosa.feature.spectral_flatness(S=S)
    cen = librosa.feature.spectral_centroid(S=S, sr=SR)
    roll = librosa.feature.spectral_rolloff(S=S, sr=SR)
    return (list(mf[1:].mean(1)) + list(con.mean(1))
            + [float(np.log(flat.mean() + 1e-12)), float(np.log(cen.mean())), float(np.log(roll.mean()))])


def describe(hot, soft):
    """None for anything unsafe to put on a module: non-finite, silent, or blown up
    (a peak over 50, a thousand times a normal output, is a runaway, not a sound)."""
    if not (np.isfinite(hot).all() and np.isfinite(soft).all()):
        return None
    if max(np.max(np.abs(hot)), np.max(np.abs(soft))) > 50:
        return None
    rh, rs = np.sqrt(np.mean(hot ** 2)), np.sqrt(np.mean(soft ** 2))
    if rh < 1e-6 or rs < 1e-7:
        return None
    tim = timbre_features(hot / rh * 0.1)
    har = harmonic_features(hot)
    dyn = [20 * math.log10(rh / rs),
           float(np.log(librosa.feature.spectral_centroid(y=hot.astype(np.float32), sr=SR).mean()
                        / librosa.feature.spectral_centroid(y=soft.astype(np.float32), sr=SR).mean()))]
    tm = time_features(hot, SIGNAL)
    f = lambda v: [float(x) for x in v]
    return dict(timbre=f(tim), harmonics=f(har), dynamics=f(dyn), time=f(tm), level=float(20 * math.log10(rh)))


GROUPS = ["timbre", "harmonics", "dynamics", "time"]


def matrix(descs):
    return {g: np.array([d[g] for d in descs], float) for g in GROUPS}


class Space:
    """Robust-scaled, group-weighted distance."""

    def __init__(self, mats):
        self.med = {g: np.median(m, 0) for g, m in mats.items()}
        self.iqr = {g: np.maximum(np.subtract(*np.percentile(m, [75, 25], 0)), 1e-6) for g, m in mats.items()}

    def z(self, mats):
        return {g: np.clip((m - self.med[g]) / self.iqr[g], -6, 6) for g, m in mats.items()}

    @staticmethod
    def dist(za, zb):
        """za, zb: dict group -> (n, d) and (m, d). Returns (n, m) distances."""
        tot = 0
        for g in GROUPS:
            a, b = za[g], zb[g]
            tot = tot + ((a[:, None, :] - b[None, :, :]) ** 2).mean(2)
        return np.sqrt(tot / len(GROUPS))


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--reuse", action="store_true", help="rescore from the cached descriptors")
    ap.add_argument("--pick", type=int, default=12, help="at most this many")
    ap.add_argument("--min-novelty", type=float, default=2.0,
                    help="each pick at least this many times the amps' own spacing from the amps")
    ap.add_argument("--family-cap", type=int, default=2, help="at most this many picks per approach")
    ap.add_argument("--dump", action="store_true", help="write every clip as a WAV, for clap_judge.py")
    ap.add_argument("--combine", action="store_true", help="select with CLAP's verdict too (after clap_judge.py)")
    ap.add_argument("--audition", action="store_true", help="render the selection, swept, to audition.wav")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    cache = OUT / "cache.json"

    global SIGNAL
    a = make_synth()
    scale_peak(a, 0.5)
    SIGNAL = np.asarray(a, np.float64)

    starter = dict(a2nb_weights(f) for f in sorted(STARTER.glob("n*.a2nb")))
    names = list(starter)
    if args.dump:
        return dump(starter, names, json.loads(cache.read_text()))
    if args.audition:
        return audition(starter)

    if args.combine:
        return score(json.loads(cache.read_text()), names, args.pick, clap=load_clap(), min_nov=args.min_novelty,
                     family_cap=args.family_cap)
    if args.reuse and cache.exists():
        C = json.loads(cache.read_text())
    else:
        amps = real_amps()
        print(f"{len(amps)} real captures to map, {len(names)} starter captures to transform")
        rng = np.random.default_rng(7)
        tmp = tempfile.mkdtemp()
        SIGNAL.astype("<f4").tofile(pathlib.Path(tmp) / "dry.f32")
        # one fixed mutation direction per capture, so its knob sweeps along a line
        noise = {n: (rng.standard_normal(1871) * np.std(starter[n])).astype("<f4") for n in names}
        jobs, meta = [], []
        for an, w in amps.items():
            for lev in (1.0, 0.1):
                jobs.append((w, None, "none", 1, lev, tmp)); meta.append(("amp", an, None, None, lev))
        # each starter capture's "partner" for PAST: the starter capture least like it (by weights is
        # meaningless, so by its plain sound — filled in after the plain renders below)
        for n in names:
            for t in T:
                for i, v in enumerate(knob_values(t)):
                    spec, S = spec_for(t, v)
                    wB = noise[n] if t[7] == "noise" else None
                    for lev in (1.0, 0.1):
                        if t[7] == "partner":
                            continue
                        jobs.append((starter[n], wB, spec, S or 1, lev, tmp)); meta.append(("cand", n, t[0], i, lev))
        with cf.ProcessPoolExecutor() as ex:
            outs = list(ex.map(render, jobs, chunksize=8))
        print(f"rendered {len(outs)} clips")
        rendered = {}
        for m, y in zip(meta, outs):
            rendered[m] = y
        # partners for PAST: the starter capture whose plain sound is furthest from this one
        amp_desc = {}
        for an in amps:
            d = describe(rendered[("amp", an, None, None, 1.0)], rendered[("amp", an, None, None, 0.1)])
            if d:
                amp_desc[an] = d
        cand_desc = {}
        for n in names:
            for t in T:
                if t[7] == "partner":
                    continue
                for i in range(len(knob_values(t))):
                    d = describe(rendered[("cand", n, t[0], i, 1.0)], rendered[("cand", n, t[0], i, 0.1)])
                    cand_desc[f"{n}|{t[0]}|{i}"] = d
        # PAST: second pass, now that plain sounds can pick each capture's partner
        sp0 = Space(matrix(list(amp_desc.values())))
        plain_z = {}
        for n in names:
            d = describe(render((starter[n], None, "none", 1, 1.0, tmp)), render((starter[n], None, "none", 1, 0.1, tmp)))
            plain_z[n] = sp0.z(matrix([d]))
        partner = {}
        for n in names:
            others = [m for m in names if m != n]
            partner[n] = max(others, key=lambda m: Space.dist(plain_z[n], plain_z[m])[0, 0])
        tp = [t for t in T if t[7] == "partner"][0]
        pj, pm = [], []
        for n in names:
            for i, v in enumerate(knob_values(tp)):
                for lev in (1.0, 0.1):
                    pj.append((starter[partner[n]], starter[n], f"morph:{v:.6g}:{v:.6g}", 1, lev, tmp)); pm.append((n, i, lev))
        with cf.ProcessPoolExecutor() as ex:
            po = list(ex.map(render, pj, chunksize=8))
        pr = {m: y for m, y in zip(pm, po)}
        for n in names:
            for i in range(len(U)):
                cand_desc[f"{n}|{tp[0]}|{i}"] = describe(pr[(n, i, 1.0)], pr[(n, i, 0.1)])
        C = dict(amps=amp_desc, cands=cand_desc, partner=partner)
        cache.write_text(json.dumps(C))

    score(C, names, args.pick)


def load_clap():
    clips = OUT / "clips"
    j = json.loads((clips / "clap.json").read_text())
    z = np.load(clips / "clap_emb.npz")
    emb = {str(n): e for n, e in zip(z["names"], z["emb"])}
    return dict(tau=j["tau"], clips=j["clips"], emb=emb)


AMP_TAGS = ("a synthesizer played through a guitar amplifier", "an overdriven electric guitar amp",
            "a clean electric guitar amplifier tone", "a fuzz guitar pedal")


def score(C, names, pick, clap=None, min_nov=0.0, family_cap=2):
    amp_names = list(C["amps"])
    A = matrix([C["amps"][a] for a in amp_names])
    keys = [k for k, d in C["cands"].items() if d]
    K = matrix([C["cands"][k] for k in keys])
    sp = Space({g: np.vstack([A[g], K[g]]) for g in GROUPS})
    zA, zK = sp.z(A), sp.z(K)

    dAA = Space.dist(zA, zA)
    np.fill_diagonal(dAA, np.inf)
    nnA = dAA.min(1)
    tau = float(np.percentile(nnA, 95))
    dKA = Space.dist(zK, zA).min(1)
    idx = {k: i for i, k in enumerate(keys)}
    level = {k: C["cands"][k]["level"] for k in keys}

    rows = []
    for n in names:
        for t in T:
            pts = [f"{n}|{t[0]}|{i}" for i in range(len(knob_values(t)))]
            if not all(p in idx for p in pts):
                rows.append(dict(capture=n, transform=t[0], family=t[1], ok=False, why="non-finite or silent"))
                continue
            ii = [idx[p] for p in pts]
            out = dKA[ii]
            zp = {g: zK[g][ii] for g in GROUPS}
            steps = [Space.dist({g: zp[g][j:j + 1] for g in GROUPS}, {g: zp[g][j + 1:j + 2] for g in GROUPS})[0, 0]
                     for j in range(len(ii) - 1)]
            path = float(sum(steps))
            lv = [level[p] for p in pts]
            frac = float(np.mean(out > tau))
            row = dict(capture=n, transform=t[0], family=t[1], frac_outside=frac,
                       novelty=float(np.median(out)), path=path,
                       evenness=float(max(steps) / path) if path > 0 else 1.0,
                       level_span=float(max(lv) - min(lv)), note=t[8], ok=True, why="")
            why = []
            if frac < 0.6:
                why.append("stays amp-like")
            if path < 1.0:
                why.append("knob barely moves it")
            if row["evenness"] > 0.6:
                why.append("one jump, not a sweep")
            if row["level_span"] > 30:
                why.append("level swings >30 dB")
            if clap is not None:
                cv = [clap["clips"].get(p) for p in pts]
                if any(v is None for v in cv):
                    why.append("no CLAP verdict")
                else:
                    moved = cv[1:]  # knob position 0 is often the plain capture
                    row["clap_outside"] = float(np.mean([v["outside"] for v in moved]))
                    row["clap_novelty"] = float(np.median([v["dist"] for v in moved]) / clap["tau"])
                    tags = [v["tag"] for v in moved if v["outside"]] or [v["tag"] for v in moved]
                    row["clap_tag"] = max(set(tags), key=tags.count)
                    if row["clap_outside"] < 0.6:
                        why.append("CLAP hears it as amp-like")
                    if row["clap_tag"] in AMP_TAGS:
                        why.append("CLAP describes it as a guitar amp or pedal")
                    e = np.array([clap["emb"][p] for p, v in zip(pts[1:], moved) if v["outside"]]
                                 or [clap["emb"][p] for p in pts[1:]])
                    m = e.mean(0)
                    row["clap_rep"] = (m / np.linalg.norm(m)).tolist()
            row["ok"], row["why"] = not why, "; ".join(why)
            # representative point: the mean of its outside points
            sel = [j for j, o in zip(ii, out) if o > tau] or ii
            row["rep"] = {g: zK[g][sel].mean(0).tolist() for g in GROUPS}
            rows.append(row)

    good = [r for r in rows if r["ok"]]
    # greedy farthest-first, at most 2 per family and 2 per capture
    chosen = []
    if good:
        rep = {g: np.array([r["rep"][g] for r in good]) for g in GROUPS}
        D = Space.dist(rep, rep) / tau
        nov = np.array([r["novelty"] / tau for r in good])
        if clap is not None:
            ce = np.array([r["clap_rep"] for r in good])
            D = (D + (1 - ce @ ce.T) / clap["tau"]) / 2
            nov = (nov + np.array([r["clap_novelty"] for r in good])) / 2
        for r, n_ in zip(good, nov):
            r["combined_novelty"] = float(n_)
        first = int(np.argmax(nov))
        chosen = [first]
        while len(chosen) < pick:
            fam = {}
            cap = {}
            for c in chosen:
                fam[good[c]["family"]] = fam.get(good[c]["family"], 0) + 1
                cap[good[c]["capture"]] = cap.get(good[c]["capture"], 0) + 1
            best, bestd = None, -1
            for j, r in enumerate(good):
                if j in chosen or fam.get(r["family"], 0) >= family_cap or cap.get(r["capture"], 0) >= 2:
                    continue
                if nov[j] < min_nov:
                    continue
                d = min(D[j, c] for c in chosen)
                if d > bestd:
                    best, bestd = j, d
            if best is None:
                break
            good[best]["spacing"] = bestd
            chosen.append(best)

    with open(OUT / "candidates.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["capture", "transform", "family", "ok", "why", "frac_outside", "novelty", "path", "evenness", "level_span"])
        for r in rows:
            w.writerow([r["capture"], r["transform"], r["family"], r["ok"], r["why"],
                        *(f"{r.get(k, float('nan')):.3f}" for k in ("frac_outside", "novelty", "path", "evenness", "level_span"))])

    print(f"\namp region: {len(amp_names)} real captures; threshold τ = {tau:.2f} "
          f"(95th percentile of their nearest-neighbour distance, median {np.median(nnA):.2f})")
    fams = {}
    for r in rows:
        if "frac_outside" in r:
            fams.setdefault(r["transform"], []).append(r)
    print("\nper transform, across the 12 captures:  outside-fraction (median) | novelty | knob travel | passes")
    for t in T:
        rs = fams.get(t[0], [])
        if rs:
            print(f"  {t[0]:18s} {np.median([r['frac_outside'] for r in rs]):4.2f} | {np.median([r['novelty'] for r in rs]):4.2f}"
                  f" | {np.median([r['path'] for r in rs]):5.2f} | {sum(r['ok'] for r in rs):2d}/12")
    print(f"\n{len(good)} candidates pass{' both judges' if clap else ''}; selected {len(chosen)}:")
    for c in chosen:
        r = good[c]
        extra = f"  CLAP: {r['clap_tag'].removeprefix('a ').removeprefix('an ')}" if clap else ""
        print(f"  {r['transform']:16s} on {r['capture']:10s} novelty {r['combined_novelty']:4.1f}×  travel {r['path']:4.1f}"
              f"  spacing {r.get('spacing', float('nan')):4.2f}  level span {r['level_span']:4.1f} dB{extra}")
    json.dump(dict(tau=tau, chosen=[{k: v for k, v in good[c].items() if k not in ('rep', 'clap_rep')} for c in chosen],
                   partner=C.get("partner", {})), open(OUT / "selection.json", "w"), indent=1)


def _jobs_tmp():
    tmp = pathlib.Path(tempfile.mkdtemp())
    SIGNAL.astype("<f4").tofile(tmp / "dry.f32")
    return tmp


def dump(starter, names, C):
    """Every clip at the hot level as a 48 kHz WAV, with a manifest, for CLAP."""
    clips = OUT / "clips"
    clips.mkdir(exist_ok=True)
    tmp = _jobs_tmp()
    rng = np.random.default_rng(7)
    noise = {n: (rng.standard_normal(1871) * np.std(starter[n])).astype("<f4") for n in names}
    jobs, keys = [], []
    for an, w in real_amps().items():
        jobs.append((w, None, "none", 1, 1.0, str(tmp))); keys.append(("amp", an))
    for n in names:
        for t in T:
            for i, v in enumerate(knob_values(t)):
                if t[7] == "partner":
                    wA, wB, spec, S = starter[C["partner"][n]], starter[n], f"morph:{v:.6g}:{v:.6g}", 1
                else:
                    spec, S = spec_for(t, v)
                    wA, wB = starter[n], (noise[n] if t[7] == "noise" else None)
                jobs.append((wA, wB, spec, S or 1, 1.0, str(tmp))); keys.append(("cand", f"{n}|{t[0]}|{i}"))
    with cf.ProcessPoolExecutor() as ex:
        outs = list(ex.map(render, jobs, chunksize=8))
    manifest = {}
    for j, ((kind, key), y) in enumerate(zip(keys, outs)):
        f = f"{j:05d}.wav"
        r = np.sqrt(np.mean(y ** 2))
        sf.write(clips / f, (y / r * 0.1 if r > 0 else y).astype(np.float32), SR, subtype="FLOAT")
        manifest[key] = dict(file=f, kind=kind)
    (clips / "manifest.json").write_text(json.dumps(manifest, indent=0))
    print(f"wrote {len(manifest)} clips to {clips}")


def audition(starter):
    """Each selected not-amp: its source capture plain, then the knob swept end to end
    over the phrase played twice. Loudness-matched, one file, with an index."""
    sel = json.loads((OUT / "selection.json").read_text())
    tmp = _jobs_tmp()
    long = np.concatenate([SIGNAL, SIGNAL])
    long.astype("<f4").tofile(tmp / "dry_long.f32")
    rng = np.random.default_rng(7)
    noise = {n: (rng.standard_normal(1871) * np.std(w)).astype("<f4") for n, w in starter.items()}
    parts, index, t0 = [], [], 0.0
    gap = np.zeros(SR // 2)

    def run(wA, wB, spec, S, sig):
        (tmp / "dry.f32").unlink(missing_ok=True)
        sig.astype("<f4").tofile(tmp / "dry.f32")
        return render((wA, wB, spec, S, 1.0, str(tmp)))

    for c in sel["chosen"]:
        n, tr = c["capture"], c["transform"]
        t = next(x for x in T if x[0] == tr)
        plain = run(starter[n], None, "none", 1, SIGNAL)
        vals = knob_values(t)
        if t[5] in ("step", "stepbin"):
            seg = len(long) // len(vals)
            sw = np.concatenate([run(starter[n], None, *spec_for(t, v)[:1], spec_for(t, v)[1] or 1,
                                     long[k * seg:(k + 1) * seg]) for k, v in enumerate(vals)])
        else:
            if t[7] == "partner":
                wA, wB = starter[sel["partner"][n]], starter[n]
            else:
                wA, wB = starter[n], (noise[n] if t[7] == "noise" else None)
            spec = spec_for(t, vals[0])[0].rsplit(":", 2)[0] + f":{vals[0]:.6g}:{vals[-1]:.6g}"
            sw = run(wA, wB, spec, 1, long)
        for label, y in ((f"{n} — plain", plain), (f"{tr} on {n} — knob swept {vals[0]:.3g} → {vals[-1]:.3g}", sw)):
            r = np.sqrt(np.mean(y ** 2))
            y = y / r * 0.1 if r > 0 else y
            index.append(f"{int(t0 // 60)}:{t0 % 60:04.1f}  {label}")
            parts += [y, gap]
            t0 += (len(y) + len(gap)) / SR
    a = np.concatenate(parts)
    a *= min(1.0, 0.95 / np.max(np.abs(a)))
    sf.write(OUT / "audition.wav", a.astype(np.float32), SR, subtype="PCM_16")
    (OUT / "audition.txt").write_text("\n".join(index) + "\n")
    print("\n".join(index))


if __name__ == "__main__":
    main()
