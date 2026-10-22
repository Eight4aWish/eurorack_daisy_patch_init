#!/usr/bin/env python3
"""
filter_dial_calibrate.py — a FILTERS network's Res dial (Mirth's "boost"), measured against
its own module: where the network's resonance falls behind the module's, the lift of its
Res input that catches it up, and the feedback that covers what the lift cannot reach.
The Wasp LP's table (main.cpp, 2026-10-22) came from tools/res_dial_calibrate.py; this is
the same method made general, for the Wasp HP and Swords.

The resonance band: at each cutoff hold of the module's test take, the band (control
test bands) where the module's own level rises most from the reference Res to its top
repeatable Res, among the bands within 15 dB of that hold's loudest (bands far below a
filter's passband are nearly empty, and their levels swing most). Its rise at each Res, averaged over the holds, is what the network must
match; the network runs on the reference take's input (the same riff in every session).

    .venv-nam/bin/python tools/filter_dial_calibrate.py hp     # Wasp HP, hp_cr_gru16_s3
    .venv-nam/bin/python tools/filter_dial_calibrate.py swords # Swords, sw4_all_gru16_s3
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
from notamp_ideas import phrase  # noqa: E402

SR = T.SR
CAP = HERE.parent / "captures"


def load(run):
    model, inputs, out, scale = C.load_model(run)
    assert inputs == ["audio", "cv", "res"], inputs
    cell = torch.nn.GRUCell(3, model.rnn.hidden_size).double()
    with torch.no_grad():
        for n in ("weight_ih", "weight_hh", "bias_ih", "bias_hh"):
            getattr(cell, n).copy_(getattr(model.rnn, f"{n}_l0").double())
    return model, cell, copy.deepcopy(model.dense).double(), scale  # a copy: .double() is in place


def run(net, x, cv, res, fb=0.0):
    """The network on x; with feedback, sample by sample (its output into its input)."""
    model, cell, dense, scale = net
    res = np.broadcast_to(np.asarray(res, np.float64), x.shape)
    if fb == 0.0:
        f = np.stack([x, cv * T.SCALE["cv"], res * T.SCALE["res"]], 1).astype(np.float32)
        with torch.no_grad():
            y, _ = model(torch.from_numpy(f)[None])
        return y[0].numpy().astype(np.float64) / scale
    h = torch.zeros(1, cell.hidden_size, dtype=torch.float64)
    y, prev = np.zeros(len(x)), 0.0
    with torch.no_grad():
        for i in range(len(x)):
            h = cell(torch.tensor([[x[i] + fb * prev, cv[i] * T.SCALE["cv"], res[i] * T.SCALE["res"]]],
                                  dtype=torch.float64), h)
            prev = float(dense(h)) / scale
            y[i] = prev
    return y


def setup(which):
    if which in ("lp", "hp"):
        W = CAP / "wasp"
        marks = list(range(11))
        mod = {r: np.load(W / f"{which}-res{r}/gru/test.npz") for r in marks}
        end = int(24.5 * SR)
        y = {r: mod[r]["y_lphp"][:end].astype(np.float64) for r in marks}
        ref = mod[6]
        return dict(net=load(W / "gru_runs" / f"{which}_cr_gru16_s3"), marks=marks, r0=6, top=10, y=y,
                    x=ref["x"][:end].astype(np.float64), cv=ref["cv_v"][:end].astype(np.float64),
                    holds=C.HOLDS, grid=np.arange(0, 20.01, 0.5), unit="dial")
    S = CAP / "swords"
    holds_v = json.loads((S / "sw-a/session.json").read_text())["test_holds_v"]
    marks = [0.0, 0.5, 1.0, 1.5, 2.5, 3.0, 3.5, 4.0]
    y = {r: np.load(S / f"sw-b-r{r:.2f}/gru/test_r{r:+.2f}_1.npz")["y_f1"].astype(np.float64) for r in marks}
    for r in (4.5, 5.5):  # oscillating, from sw-a (a day earlier): for the feedback's comparison
        y[r] = np.load(S / f"sw-a/gru/test_r{r:+.2f}_1.npz")["y_f1"].astype(np.float64)
    ref = np.load(S / "sw-b-r3.00/gru/test_r+3.00_1.npz")
    holds = C.swords_holds(holds_v)
    holds = {v: t for v, t in holds.items() if v != holds_v[-1]}  # the top hold oscillates at 4 V
    return dict(net=load(S / "gru_runs/sw4_all_gru16_s3"), marks=marks + [4.5, 5.5], r0=3.0, top=4.0, y=y,
                x=ref["x"].astype(np.float64), cv=ref["cv_v"].astype(np.float64), holds=holds,
                grid=np.arange(0, 8.01, 0.25), unit="V", holds_v=holds_v)


def main():
    which = sys.argv[1]
    s = setup(which)
    lev = {r: C.band_levels_at(y, s["holds"]) for r, y in s["y"].items()}
    r0, top = s["r0"], s["top"]
    def pick(v):
        rise_v = lev[top][v] - lev[r0][v]
        loud = lev[top][v] >= lev[top][v].max() - 15.0
        return int(np.argmax(np.where(loud, rise_v, -np.inf)))
    band = {v: pick(v) for v in s["holds"]}
    print(f"{which}: resonance band per hold", {round(v, 1): C.BANDS[b] for v, b in band.items()})
    if which == "swords":  # its natural frequency (step 0), the band that should hold it
        nat = {round(v, 1): round(168.9 * 2 ** (0.972 * (v - 4))) for v in s["holds"]}
        print("  Swords' natural frequency at each hold, Hz:", nat)
    rise = lambda L, base: float(np.mean([L[v][band[v]] - base[v][band[v]] for v in s["holds"]]))  # noqa: E731
    mod = {r: rise(lev[r], lev[r0]) for r in s["marks"]}
    net0 = C.band_levels_at(run(s["net"], s["x"], s["cv"], r0), s["holds"])
    netr = {g: rise(C.band_levels_at(run(s["net"], s["x"], s["cv"], g), s["holds"]), net0) for g in s["grid"]}
    peak = max(netr, key=netr.get)
    print(f"network's rise by Res input ({s['unit']}):", "  ".join(f"{g:g}:{netr[g]:+.2f}" for g in s["grid"][::2]))
    print(f"its resonance rises at most {netr[peak]:+.2f} dB, at Res input {peak:g}\n")
    print(f"  module Res   module rise   network at the same Res   Res input that matches   feedback needed at {peak:g}")
    for r in s["marks"]:
        if r < r0:
            continue
        same = netr.get(float(r), float("nan"))
        best = min((g for g in s["grid"] if g <= peak), key=lambda g: abs(netr[g] - mod[r]))
        fbtxt = ""
        if mod[r] > netr[peak] + 0.25:
            trials = {}
            for fb in (0.05, 0.1, 0.15, 0.2, 0.3):
                trials[fb] = rise(C.band_levels_at(run(s["net"], s["x"], s["cv"], peak, fb), s["holds"]), net0)
                if trials[fb] >= mod[r]:
                    break
            fbtxt = "  ".join(f"{fb:g}:{v:+.2f}" for fb, v in trials.items())
        print(f"  {r:6.1f}       {mod[r]:+6.2f}          {same:+6.2f}                  {best:5.2f}"
              f"                   {fbtxt}")
    if which == "swords":
        # Where the feedback makes it oscillate, against Swords' own self-oscillation: pitch
        # 168.9 Hz at V/OCT +4 V rising 0.972 octave per volt (step 0), from Res CV 4.2-5.4 V.
        from notamp_spectra import peak as tone
        xp = phrase(); n = len(xp)
        cut = np.linspace(2.5, 10.5, n)
        for fb in (0.1, 0.2, 0.3, 0.5):
            y = run(s["net"], xp, cut, np.full(n, top), fb)
            notes = np.sqrt(np.mean(y[np.abs(xp) > 0.05] ** 2)); cells = []
            for k in range(16):
                a, b = int((k * 0.5 + 0.425) * SR), int((k * 0.5 + 0.495) * SR)
                lvl = 20 * np.log10(np.sqrt(np.mean(y[a:b] ** 2)) / notes + 1e-12)
                v = cut[(a + b) // 2]
                if lvl > -15:
                    f0, h = tone(y[a:b], lo=25)
                    want = 168.9 * 2 ** (0.972 * (v - 4))
                    cells.append(f"{v:.1f}V {f0:.0f}/{want:.0f}")
            print(f"  feedback {fb}: own tone at (cutoff: tone/Swords' own) " + (", ".join(cells) or "none"))


if __name__ == "__main__":
    main()
