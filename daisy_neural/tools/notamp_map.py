#!/usr/bin/env python3
"""
notamp_map.py — the not-amp search's map, flattened to two dimensions, for the
MirthNotAmps animation (eight4awish-video, slides/mirth-animations.md, animation 2).

Reads the search's cache (amp_compare/notamp_search/cache.json: every real A2 capture,
and every transform on each starter capture at five steer positions, described as
tools/notamp_search.py describes them), measures the four weight steers the same way,
and writes JSON:

    tau        the amps' own spacing: 95% of real captures have a neighbour this close
    amps       every real capture as an anonymous point (measured, never shipped)
    starters   the twelve starter captures at their plain sound
    routes     for each of the sixteen not-amps on the card, and for each transform that
               measured as still an amp, its five steer positions: 2-D position, distance
               to the nearest real capture in amp spacings, and whether that is outside

Distances and "outside" are the search's own, in its full descriptor space (Space,
GROUPS, the same robust scaling over amps and candidates). The 2-D positions are the
first two principal components of that same weighted space: a picture, not the measure.

The weight steers (RND1/4/6/7 SCALE, captures/random/rNN_x2p0.a2nb) were built after
the search, so they are rendered here through the same harness (tools/a2_explore_s1),
every weight scaled as the module's steer scales it (×0.75 → ×1.5 of the ×2 file, log),
and each render levelled to a 0.5 peak first, as the module's level table levels it.

    .venv-nam/bin/python tools/notamp_map.py <out.json>
"""
import json
import math
import pathlib
import sys
import tempfile

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import notamp_search as S  # noqa: E402

PROJECT = HERE.parent
CACHE = PROJECT / "amp_compare" / "notamp_search" / "cache.json"

# the card's not-amps (src/notamps.h): OLED name -> (capture, the search's transform)
CARD = {
    "SINE BUG": ("BUGERA G5", "sine neurons"),
    "LINEAR TR": ("TWO ROCK", "slope→linear"),
    "LINEAR TRY": ("TRAYNOR", "slope→linear"),
    "RECT ORG": ("ORANGE TH", "slope→rectify"),
    "FB100 F57": ("FENDER 57", "feedback 100 Hz"),
    "FRZ E TR": ("TWO ROCK", "freeze early"),
    "FRZ M F57": ("FENDER 57", "freeze mid"),
    "FRZ M KAY": ("KAY 703", "freeze mid"),
    "FOLD ORG": ("ORANGE TH", "fold inside"),
    "RATE SVT": ("SVT-2 PRO", "rate ÷"),
    "PAST BLU": ("BLUESBRKR", "past a partner"),
    "PAST PLX": ("PLEXI LORE", "past a partner"),
}
RND = {"RND1 SCALE": 1, "RND4 SCALE": 4, "RND6 SCALE": 6, "RND7 SCALE": 7}
STAY = ["bias off", "bias ×4", "mix-in off", "mix-in ×6", "rectified mix-in", "delayed mix-in",
        "lane offset", "long gaps out"]


def measure_rnd():
    """The four weight steers, described as the search describes everything: {"RNDn SCALE|i":
    (descriptors, weight scale)} at the five steer positions, each render levelled first."""
    dry = np.asarray(S.make_synth(), np.float64)
    S.scale_peak(dry, 0.5)
    S.SIGNAL = dry                                # describe() reads the dry phrase from here
    tmp = tempfile.mkdtemp()
    dry.astype("<f4").tofile(pathlib.Path(tmp) / "dry.f32")
    out = {}
    for name, seed in RND.items():
        base = np.frombuffer((PROJECT / "captures" / "random" / f"r{seed:02d}_x2p0.a2nb").read_bytes()[32:], "<f4")
        for i, u in enumerate(S.U):
            s = math.exp(math.log(0.75) + (math.log(1.5) - math.log(0.75)) * u)
            w = (base * s).astype("<f4")
            hot = S.render((w, None, "none", 1, 1.0, tmp))
            soft = S.render((w, None, "none", 1, 0.1, tmp))
            g = 0.5 / max(np.max(np.abs(hot)), 1e-12)
            d = S.describe(hot * g, soft * g)
            if d is None:
                raise SystemExit(f"{name} at ×{2 * s:.2f}: no usable render")
            out[f"{name}|{i}"] = (d, 2 * s)
    return out


def weighted(z):
    """Concatenate the groups so plain Euclidean distance equals Space.dist."""
    return np.hstack([z[g] / math.sqrt(len(S.GROUPS) * z[g].shape[1]) for g in S.GROUPS])


def main():
    out_path = pathlib.Path(sys.argv[1])
    C = json.loads(CACHE.read_text())
    amp_names = list(C["amps"])
    A = S.matrix([C["amps"][a] for a in amp_names])
    keys = [k for k, d in C["cands"].items() if d]
    K = S.matrix([C["cands"][k] for k in keys])
    sp = S.Space({g: np.vstack([A[g], K[g]]) for g in S.GROUPS})
    zA, zK = sp.z(A), sp.z(K)
    dAA = S.Space.dist(zA, zA)
    np.fill_diagonal(dAA, np.inf)
    tau = float(np.percentile(dAA.min(1), 95))

    # the weight steers, measured as the search measured everything else
    rnd_desc = measure_rnd()
    R = S.matrix([d for d, _ in rnd_desc.values()])
    zR = sp.z(R)

    # the picture: principal components of amps and candidates, in the weighted space
    XA, XK, XR = weighted(zA), weighted(zK), weighted(zR)
    X = np.vstack([XA, XK])
    mu = X.mean(0)
    _, sv, vt = np.linalg.svd(X - mu, full_matrices=False)
    P = vt[:2].T
    explained = float((sv[:2] ** 2).sum() / (sv ** 2).sum())
    xy = lambda M: ((M - mu) @ P).round(4).tolist()

    near = lambda Z: (S.Space.dist(Z, zA).min(1) / tau)
    idx = {k: i for i, k in enumerate(keys)}

    def path(capture, transform):
        t = next(t for t in S.T if t[0] == transform)
        pts = [f"{capture}|{transform}|{i}" for i in range(len(S.knob_values(t)))]
        if not all(p in idx for p in pts):
            return None
        ii = [idx[p] for p in pts]
        Z = {g: zK[g][ii] for g in S.GROUPS}
        d = near(Z)
        return dict(capture=capture, transform=transform, family=t[1],
                    steer=[round(v, 4) for v in S.knob_values(t)],
                    xy=xy(XK[ii]), dist=d.round(3).tolist(), outside=(d > 1).tolist())

    routes = {}
    for oled, (cap, tr) in CARD.items():
        r = path(cap, tr)
        if r is None:
            raise SystemExit(f"{oled}: {cap} | {tr} missing from the cache")
        routes[oled] = r
    for name in RND:
        ii = [list(rnd_desc).index(f"{name}|{i}") for i in range(len(S.U))]
        Z = {g: zR[g][ii] for g in S.GROUPS}
        d = near(Z)
        routes[name] = dict(capture=f"{name.split()[0]} X2", transform="every weight scaled", family="weights",
                            steer=[round(rnd_desc[f"{name}|{i}"][1], 4) for i in range(len(S.U))],
                            xy=xy(XR[ii]), dist=d.round(3).tolist(), outside=(d > 1).tolist())
    stays = [r for cap in sorted({c for c, _ in CARD.values()} | set(C["partner"])) for tr in STAY
             if (r := path(cap, tr)) is not None]

    starters = {}
    for cap in C["partner"]:
        k = f"{cap}|slope→linear|0"               # slope 0.01: the capture as trained
        starters[cap] = xy(XK[[idx[k]]])[0]

    out = dict(tau=round(tau, 4), explained=round(explained, 3), amps=xy(XA), starters=starters,
               partner=C["partner"], routes=routes, stays=stays,
               counts=dict(amps=len(amp_names), clips=2 * (len(keys) + len(amp_names))))
    out_path.write_text(json.dumps(out))
    print(f"tau {tau:.3f}; 2-D view keeps {100 * explained:.0f}% of the spread; "
          f"{len(amp_names)} amps, {len(routes)} not-amps, {len(stays)} stay-amp paths")
    for k, r in routes.items():
        print(f"  {k:11s} {r['capture']:11s} {r['transform']:20s} distance {' '.join(f'{v:5.1f}' for v in r['dist'])}")
    sd = np.array([max(r["dist"]) for r in stays])
    print(f"  stay-amp paths: furthest point median {np.median(sd):.2f}, max {sd.max():.2f} spacings; "
          f"{np.mean(sd > 1) * 100:.0f}% ever outside")


if __name__ == "__main__":
    main()
