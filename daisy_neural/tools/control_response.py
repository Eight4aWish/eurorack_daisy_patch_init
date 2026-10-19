#!/usr/bin/env python3
"""
control_response.py — do a network's controls move the sound the way the Wasp's do?

Added 2026-10-19, after the first playable GRU went onto the module: its cutoff played,
its Res did nothing audible, and every score had passed it, because ESR counts phase
the ear ignores and an averaged Wasp keeps ESR low. This test judges what is heard.

The `test` take was recorded with identical input at every Res mark, its cutoff held at
2, 4, 6, 8 and 10 V in turn. For each run, the network is driven with that same input
at each mark (Res 5 included, which no network trained on) and its level is measured in
seven bands during each hold, as is the real Wasp's at the same mark. Then:

  res_err_db    the change Res makes (each mark against Res 6, band by band, at every
                hold), network against Wasp: mean absolute difference, dB
  res_move      how far the network's sound moves with Res, over how far the Wasp's does
                (1 = the right amount; the first GRU-16 was well under)
  cut_err_db    the same for the cutoff: each hold against the 6 V hold, at every mark
  cut_move      and its amount

Run in .venv-nam:

    .venv-nam/bin/python tools/control_response.py captures/wasp/gru_runs/lp2_* \\
        captures/wasp/gru_runs/lp_cr_gru16_s1
"""

import argparse
import copy
import json
import pathlib
import sys

import numpy as np
import torch
from scipy.signal import welch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import train_gru as T  # noqa: E402

SR = T.SR
MARKS = (0, 2, 4, 5, 6, 8, 10)
HOLDS = {2: (6.3, 7.9), 4: (10.3, 11.9), 6: (14.3, 15.9), 8: (18.3, 19.9), 10: (22.3, 23.9)}  # volts: seconds
BANDS = ((50, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000))


def band_levels(y):
    """{hold volts: array of band levels in dB} for one test-take output."""
    out = {}
    for v, (a, b) in HOLDS.items():
        f, p = welch(y[int(a * SR):int(b * SR)], SR, nperseg=4096)
        p = 10 * np.log10(p + 1e-20)
        out[v] = np.array([np.mean(p[(f >= lo) & (f < hi)]) for lo, hi in BANDS])
    return out


def load_model(run):
    cfg = json.loads((run / "config.json").read_text())
    inputs = cfg["inputs"].split(",")
    if cfg.get("arch", "rnn") == "wavenet":
        film = cfg.get("film", "activation_pre")
        m = T.WaveNetModel(len(inputs), "" if film == "none" else film)
    else:
        m = T.RNNModel(len(inputs), cfg["hidden"], cfg.get("cell", "gru"), cfg.get("layers", 1))
    m.load_state_dict(torch.load(run / "best.pt", map_location="cpu"))
    norm = run / "norm.json"
    scale = json.loads(norm.read_text())["target_scale"] if norm.exists() else 1.0
    return m, inputs, cfg.get("output", "lphp"), scale


def measure(run, captures):
    model, inputs, out, scale = load_model(run)
    if "res" not in inputs:
        return None  # a cutoff-only network: no Res to test
    reference = T.Take(captures / "lp-res6" / "gru" / "test.npz")
    real, net = {}, {}
    for r in MARKS:
        real[r] = band_levels(T.Take(captures / f"lp-res{r}" / "gru" / "test.npz").target(0, reference.n, out))
        t = copy.copy(reference)
        t.res = float(r)  # the same input; only the Res input changes
        _, o = T.evaluate(model, [(t, 0, t.n, T.SETTLE, False)], inputs, out, "cpu", keep=True, out_scale=scale)
        net[r] = band_levels(o[0])
    res_d = [(net[r][v] - net[6][v], real[r][v] - real[6][v]) for r in MARKS if r != 6 for v in HOLDS]
    cut_d = [(net[r][v] - net[r][6], real[r][v] - real[r][6]) for r in MARKS for v in HOLDS if v != 6]
    held = [(net[5][v] - net[6][v], real[5][v] - real[6][v]) for v in HOLDS]

    def summary(pairs):
        n, w = np.array([p[0] for p in pairs]), np.array([p[1] for p in pairs])
        return float(np.mean(np.abs(n - w))), float(np.mean(np.abs(n)) / np.mean(np.abs(w)))

    res_err, res_move = summary(res_d)
    cut_err, cut_move = summary(cut_d)
    held_err, _ = summary(held)
    return {"res_err_db": round(res_err, 2), "res_move": round(res_move, 2),
            "cut_err_db": round(cut_err, 2), "cut_move": round(cut_move, 2),
            "res5_err_db": round(held_err, 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--captures", default=str(T.CAPTURES))
    args = ap.parse_args()
    captures = pathlib.Path(args.captures)
    print(f"{'run':24s} {'Res err dB':>10s} {'Res move':>9s} {'cut err dB':>11s} {'cut move':>9s} {'Res 5 err dB':>13s}")
    for r in map(pathlib.Path, args.runs):
        if not (r / "best.pt").exists():
            continue
        m = measure(r, captures)
        if m is None:
            continue
        (r / "control_response.json").write_text(json.dumps(m, indent=1) + "\n")
        print(f"{r.name:24s} {m['res_err_db']:10.2f} {m['res_move']:9.2f} {m['cut_err_db']:11.2f} "
              f"{m['cut_move']:9.2f} {m['res5_err_db']:13.2f}", flush=True)


if __name__ == "__main__":
    main()
