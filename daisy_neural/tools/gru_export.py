#!/usr/bin/env python3
"""
gru_export.py — a train_gru.py run's best epoch as a .grnb file for the card.

Run it in .venv-nam (it needs PyTorch to read best.pt):

    .venv-nam/bin/python tools/gru_export.py captures/wasp/gru_runs/lp_cr_gru24_s2 --name "WASP LP CR"

Only what the Daisy engine runs is accepted: a GRU, one layer, a hidden size that is a
multiple of 4 up to 32. The input scales are train_gru.py's own, so the firmware hands
the engine plain volts and dial positions.

--layer 9,10 adds the Wasp's granular layer built from those Res marks' repeat takes
(tools/wasp_layer.py), making a version-2 file. It needs the captures, so run it on the
machine that has them.
"""

import argparse
import json
import pathlib
import sys

import torch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import grnb  # noqa: E402
from train_gru import SCALE  # noqa: E402


def export(run, name, gain=1.0, layer=None):
    cfg = json.loads((run / "config.json").read_text())
    if cfg.get("arch", "rnn") != "rnn" or cfg.get("cell", "gru") != "gru" or cfg.get("layers", 1) != 1:
        raise ValueError(f"{run.name}: only a one-layer GRU runs on the Daisy")
    inputs = cfg["inputs"].split(",")
    sd = torch.load(run / "best.pt", map_location="cpu")
    w = [sd["rnn.weight_ih_l0"], sd["rnn.weight_hh_l0"], sd["rnn.bias_ih_l0"], sd["rnn.bias_hh_l0"],
         sd["dense.weight"].reshape(-1), sd["dense.bias"].reshape(-1)]
    flat = torch.cat([t.reshape(-1) for t in w]).tolist()
    # A run trained on a normalised target (--target-dbfs) plays at the Wasp's own level
    # once its output is divided by that scale, which the file's gain carries.
    norm = run / "norm.json"
    if norm.exists():
        gain = gain / json.loads(norm.read_text())["target_scale"]
    return grnb.pack(name, gain, cfg["hidden"], inputs, [SCALE[k] for k in inputs], flat, layer)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run", help="a run folder under captures/wasp/gru_runs")
    ap.add_argument("--name", required=True, help=f"the name on the module, up to {grnb.NAME_LEN - 1} characters")
    ap.add_argument("--gain", type=float, default=1.0)
    ap.add_argument("--out", help="the .grnb to write (default: <run>/<run name>.grnb)")
    ap.add_argument("--layer", help="Res marks for the granular layer, e.g. 9,10")
    args = ap.parse_args()
    run = pathlib.Path(args.run)
    layer = None
    if args.layer:
        import wasp_layer
        layer = wasp_layer.pack([wasp_layer.build_bank(int(r)) for r in args.layer.split(",")])
    data = export(run, args.name, args.gain, layer)
    out = pathlib.Path(args.out) if args.out else run / f"{run.name}.grnb"
    out.write_bytes(data)
    name, _, hidden, kinds, scales, weights, lay = grnb.unpack(data, with_layer=True)
    print(f"{run.name} -> {out}  '{name}', GRU-{hidden}, inputs {kinds} scaled {scales}, {len(weights)} weights, "
          f"layer {len(lay) if lay else 0} bytes, {len(data)} bytes")


if __name__ == "__main__":
    main()
