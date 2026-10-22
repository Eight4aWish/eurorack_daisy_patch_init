#!/usr/bin/env python3
"""
swords_fixed_eval.py — the fixed-Res families (SWORDS.md, "Fixed Res"), judged as designed:

1. each network against Swords at its own Res: ESR on its session's test take;
2. each family's Res response, Part A's control test cells (band levels per cutoff hold,
   changes from Res 3 V, the judged region only): network at r against Swords' take at r;
3. the held-out 2 V: the 1.5 and 2.5 V networks blended (outputs crossfaded; for the
   shared family also weights averaged, fed Res 2), against this morning's 2 V takes
   (sw-b-d2, sw-b-d3), beside each one alone and the Res-input networks (sw4_*, Res 0-4 V).

    .venv-nam/bin/python tools/swords_fixed_eval.py        # on the mini, where the runs are
"""

import copy
import json
import pathlib
import sys

import numpy as np
import torch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import control_response as C  # noqa: E402
import train_gru as T  # noqa: E402

CAP = HERE.parent / "captures/swords"
RUNS = CAP / "gru_runs"
RES = [0.0, 0.5, 1.0, 1.5, 2.5, 3.0, 3.5, 4.0]
SR = T.SR


def take(session, name):
    return np.load(CAP / session / "gru" / f"{name}.npz")


def run_net(model, scale, x, cv, res):
    feats = np.stack([x, cv * T.SCALE["cv"], res * T.SCALE["res"]], axis=1).astype(np.float32)
    with torch.no_grad():
        y, _ = model(torch.from_numpy(feats)[None])
    return y[0].numpy().astype(np.float64) / scale


def load(name):
    model, inputs, out, scale = C.load_model(RUNS / name)
    assert inputs == ["audio", "cv", "res"], (name, inputs)
    return model, scale


def esr(y, ref, a=int(0.3 * SR)):
    return float(np.sum((y[a:] - ref[a:]) ** 2) / np.sum(ref[a:] ** 2))


def main():
    holds_v = json.loads((CAP / "sw-a/session.json").read_text())["test_holds_v"]
    holds = C.swords_holds(holds_v)
    top = holds_v[-1]
    judged = lambda r, v: r <= 3.5 or (r == 4.0 and v != top)  # noqa: E731  (sw-a region.json)
    tests = {r: take(f"sw-b-r{r:.2f}", f"test_r{r:+.2f}_1") for r in RES}

    print("1. each network against Swords at its own Res (ESR on its test take)")
    print("   Res     independent   shared copy   parent alone")
    parent = load("swf_parent_gru16_s1")
    fams = {"ind": {}, "ft": {}}
    for r in RES:
        z = tests[r]
        x, cv, ref = (z[k].astype(np.float64) for k in ("x", "cv_v", "y_f1"))
        row = []
        for fam in ("ind", "ft"):
            m = load(f"swf_{fam}_r{r:.2f}_gru16_s1")
            y = run_net(*m, x, cv, np.full(len(x), r))
            fams[fam][r] = (m, y)
            row.append(esr(y, ref))
        row.append(esr(run_net(*parent, x, cv, np.full(len(x), r)), ref))
        print(f"   {r:3.1f}      " + "      ".join(f"{e:8.4f}" for e in row))

    print("\n2. each family's Res response (changes from Res 3 V, judged cells), as Part A's control test")
    real = {r: C.band_levels_at(tests[r]["y_f1"].astype(np.float64), holds) for r in RES}
    for fam, label in (("ind", "independent"), ("ft", "shared")):
        net = {r: C.band_levels_at(fams[fam][r][1], holds) for r in RES}
        cells = {f"R{r:g} {v:g}V": (net[r][v] - net[3.0][v], real[r][v] - real[3.0][v])
                 for r in RES if r != 3.0 for v in holds_v if judged(r, v) and judged(3.0, v)}
        s = C.score(cells)
        print(f"   {label:12s} right way {s['right_way']:.2f}  amount {s['amount']:.2f}  "
              f"err {s['err_db']:.2f} dB  spurious {s['spurious']:.2f}   ({s['audible_cells']} audible cells)")
    print("   for comparison, Part A's Res-input networks on sw-a: 30 min 0.75 / 0.69 / spurious 0.11,"
          " 60 min 0.61 / 0.35 / 0.07 (medians)")

    print("\n3. the held-out 2 V, against this morning's two 2 V takes (ESR vs d2, vs d3)")
    d2, d3 = take("sw-b-d2", "test_r+2.00_1"), take("sw-b-d3", "test_r+2.00_1")
    x, cv = d2["x"].astype(np.float64), d2["cv_v"].astype(np.float64)
    r2, r3 = d2["y_f1"].astype(np.float64), d3["y_f1"].astype(np.float64)
    n = len(x)
    cand = {}
    for fam, label in (("ind", "independent"), ("ft", "shared")):
        (m15, s15), (m25, s25) = fams[fam][1.5][0], fams[fam][2.5][0]
        y15 = run_net(m15, s15, x, cv, np.full(n, 1.5))
        y25 = run_net(m25, s25, x, cv, np.full(n, 2.5))
        cand[f"{label}: the 1.5 V network alone"] = y15
        cand[f"{label}: the 2.5 V network alone"] = y25
        cand[f"{label}: 1.5 and 2.5 crossfaded"] = 0.5 * (y15 + y25)
        if fam == "ft":
            avg = copy.deepcopy(m15)
            sd15, sd25 = m15.state_dict(), m25.state_dict()
            avg.load_state_dict({k: 0.5 * (sd15[k] + sd25[k]) for k in sd15})
            cand["shared: weights averaged, at Res 2"] = run_net(avg, s15, x, cv, np.full(n, 2.0))
    cand["the parent, at Res 2"] = run_net(*parent, x, cv, np.full(n, 2.0))
    for name in sorted(p.name for p in RUNS.glob("sw4_*_gru*")):
        if (RUNS / name / "best.pt").exists():
            cand[f"Res-input {name}, at Res 2 (held out)"] = run_net(*load(name), x, cv, np.full(n, 2.0))
    print(f"   Swords against itself (d2 vs d3): {esr(r3, r2):.4f}")
    for k, y in cand.items():
        print(f"   {k:48s} {esr(y, r2):.4f}  {esr(y, r3):.4f}")


if __name__ == "__main__":
    main()
