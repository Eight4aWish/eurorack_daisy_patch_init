#!/usr/bin/env python3
"""
control_response.py — do a network's controls move the sound the way the Wasp's do?

Added 2026-10-19, after the first playable GRU went onto the module: its cutoff played,
its Res did nothing audible, and every score had passed it, because ESR counts phase
the ear ignores and an averaged Wasp keeps ESR low. This test judges what is heard.

The `test` take was recorded with identical input at every Res mark, its cutoff held at
2, 4, 6, 8 and 10 V in turn. For each run, the network is driven with that same input
at each mark (Res 5 included, which no network trained on) and its level is measured in
seven bands during each hold, as is the real Wasp's at the same mark. A cell is one
(mark, hold, band); its change is the level there against Res 6 (for Res) or against the
6 V hold (for the cutoff), network and Wasp alike.

The first version averaged every cell, and passed the first GRUs whose Res was inaudible
on the module: most cells are ones where the real Wasp's Res changes nothing either. So
the verdict now comes from the AUDIBLE cells, where the real Wasp changes by 3 dB or more:

  right_way     share of audible cells where the network moves the same way by 1 dB or more
  amount        the median of the network's change over the Wasp's there (1 = right,
                0 = no movement, below 0 = the wrong way)
  err_db        mean absolute difference there, dB
  spurious      share of the QUIET cells (the Wasp moves under 1 dB) where the network
                moves 3 dB or more

for Res and for the cutoff, plus the first version's all-cell figures. The per-cell
changes are kept in each run's control_response.json, so a run can be re-scored without
running it again.

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
    elif cfg.get("arch") == "hyper":
        m = T.HyperRNN(len(inputs), cfg["hidden"], cfg.get("hyper_degree", 2))
    else:
        m = T.RNNModel(len(inputs), cfg["hidden"], cfg.get("cell", "gru"), cfg.get("layers", 1))
    m.load_state_dict(torch.load(run / "best.pt", map_location="cpu"))
    norm = run / "norm.json"
    scale = json.loads(norm.read_text())["target_scale"] if norm.exists() else 1.0
    return m, inputs, cfg.get("output", "lphp"), scale


def measure(run, captures):
    model, inputs, out, scale = load_model(run)
    if "res" not in inputs and not hasattr(model, "set_res"):
        return None  # a cutoff-only network: no Res to test
    reference = T.Take(captures / "lp-res6" / "gru" / "test.npz")
    real, net = {}, {}
    for r in MARKS:
        real[r] = band_levels(T.Take(captures / f"lp-res{r}" / "gru" / "test.npz").target(0, reference.n, out))
        t = copy.copy(reference)
        t.res = float(r)  # the same input; only the Res input changes
        _, o = T.evaluate(model, [(t, 0, t.n, T.SETTLE, False)], inputs, out, "cpu", keep=True, out_scale=scale)
        net[r] = band_levels(o[0])
    res_cells = {f"R{r} {v}V": (net[r][v] - net[6][v], real[r][v] - real[6][v]) for r in MARKS if r != 6 for v in HOLDS}
    cut_cells = {f"R{r} {v}V": (net[r][v] - net[r][6], real[r][v] - real[r][6]) for r in MARKS for v in HOLDS if v != 6}
    return {"res": score(res_cells), "cut": score(cut_cells),
            "cells": {"bands_hz": BANDS,
                      "res": {k: [np.round(a, 2).tolist(), np.round(b, 2).tolist()] for k, (a, b) in res_cells.items()},
                      "cut": {k: [np.round(a, 2).tolist(), np.round(b, 2).tolist()] for k, (a, b) in cut_cells.items()}}}


def score(cells, audible=3.0, quiet=1.0):
    """The verdict on one control from its cells {name: (network change, Wasp change)}, each
    an array over the bands, in dB."""
    n = np.concatenate([c[0] for c in cells.values()])
    w = np.concatenate([c[1] for c in cells.values()])
    loud, calm = np.abs(w) >= audible, np.abs(w) < quiet
    right = (np.sign(n) == np.sign(w)) & (np.abs(n) >= 1.0)
    return {"audible_cells": int(loud.sum()),
            "right_way": round(float(np.mean(right[loud])), 2) if loud.any() else None,
            "amount": round(float(np.median(n[loud] / w[loud])), 2) if loud.any() else None,
            "err_db": round(float(np.mean(np.abs(n[loud] - w[loud]))), 2) if loud.any() else None,
            "spurious": round(float(np.mean(np.abs(n[calm]) >= audible)), 2) if calm.any() else None,
            "all_err_db": round(float(np.mean(np.abs(n - w))), 2),
            "all_move": round(float(np.mean(np.abs(n)) / np.mean(np.abs(w))), 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--captures", default=str(T.CAPTURES))
    args = ap.parse_args()
    captures = pathlib.Path(args.captures)
    print(f"{'':24s} {'RES (audible cells)':^37s}  {'CUTOFF (audible cells)':^37s}")
    print(f"{'run':24s} " + "  ".join(f"{'right way':>9s} {'amount':>7s} {'err dB':>7s} {'spurious':>8s}" for _ in range(2)))
    for r in map(pathlib.Path, args.runs):
        if not (r / "best.pt").exists():
            continue
        m = measure(r, captures)
        if m is None:
            continue
        (r / "control_response.json").write_text(json.dumps(m, indent=1) + "\n")
        print(f"{r.name:24s} " + "  ".join(f"{m[k]['right_way']:9.2f} {m[k]['amount']:7.2f} {m[k]['err_db']:7.2f} "
                                           f"{m[k]['spurious']:8.2f}" for k in ("res", "cut")), flush=True)


if __name__ == "__main__":
    main()
