#!/usr/bin/env python3
"""
gru_check.py — does the Daisy's GRU engine give PyTorch's output? The brief's pipeline
check, run before any network is flashed.

For each take: exports the run to .grnb, runs it through gru_host (the firmware's engine,
compiled for the desktop) with exact and with table activations, runs the same take
through PyTorch (train_gru.py's own evaluate()), and reports how far apart they are,
next to how far each is from the real Wasp. Run in .venv-nam, after building both
gru_host builds (see tools/gru_host.cpp):

    .venv-nam/bin/python tools/gru_check.py captures/wasp/gru_runs/lp_cr_gru24_s2 \\
        captures/wasp/lp-res6/gru/test.npz captures/wasp/lp-res10/gru/test.npz
"""

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import torch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import train_gru as T  # noqa: E402
from gru_export import export  # noqa: E402


def esr(a, b, skip=T.SETTLE):
    a, b = a[skip:].astype(np.float64), b[skip:].astype(np.float64)
    return float(np.sum((a - b) ** 2) / np.sum(b ** 2))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("takes", nargs="+", help="gru/*.npz takes to run")
    args = ap.parse_args()
    run = pathlib.Path(args.run)
    cfg = json.loads((run / "config.json").read_text())
    inputs = cfg["inputs"].split(",")
    model = T.RNNModel(len(inputs), cfg["hidden"])
    model.load_state_dict(torch.load(run / "best.pt", map_location="cpu"))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "net.grnb").write_bytes(export(run, "CHECK"))
        print(f"{run.name}: GRU-{cfg['hidden']}, inputs {inputs}")
        print(f"{'take':24s} {'C++ exact vs PyTorch':>22s} {'C++ table vs PyTorch':>22s} "
              f"{'PyTorch vs Wasp':>16s} {'C++ table vs Wasp':>18s}")
        for spec in args.takes:
            take = T.Take(spec)
            raw = {"audio": take.data[:, 0], "cv": take.data[:, 1], "res": np.full(take.n, take.res, np.float32)}
            np.stack([raw[k] for k in inputs], axis=1).astype("<f4").tofile(tmp / "in.f32")
            outs = {}
            for build in ("gru_host_exact", "gru_host"):
                subprocess.run([str(HERE / build), str(tmp / "net.grnb"), str(tmp / "in.f32"), str(tmp / "out.f32")],
                               check=True, capture_output=True)
                outs[build] = np.fromfile(tmp / "out.f32", dtype="<f4")
            _, keep = T.evaluate(model, [(take, 0, take.n, T.SETTLE, False)], inputs, cfg["output"], "cpu", keep=True)
            py, wasp = keep[0], take.target(0, take.n, cfg["output"])
            line = f"{take.label:24s}"
            for build in ("gru_host_exact", "gru_host"):
                d = np.abs(outs[build] - py)
                line += f"  max {d.max():.1e}, ESR {esr(outs[build], py):.1e}"
            print(line + f"  {esr(py, wasp):16.4f}  {esr(outs['gru_host'], wasp):18.4f}")


if __name__ == "__main__":
    main()
