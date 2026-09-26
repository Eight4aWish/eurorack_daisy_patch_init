#!/usr/bin/env python3
"""
nam_to_a2nb.py — a downloaded .nam straight onto the card.

TONE3000 carries 700,000+ tones and A2 models are filterable by architecture,
so the useful workflow is: download a .nam, run this, copy to the card. No C++,
no rebuild, no reflash.

The alternative path — nam_to_cpp_array.py into model_data_nam_a2.h, then
export_captures.py — means editing a header and recompiling for every capture,
which stops being reasonable at more than about five.

What it extracts:

    SlimmableContainer   an A2 download is ONE file holding both sizes. We take
                         the submodel whose max_value is nearest 0.5, which is
                         A2-Lite at 3 channels — the only size the embedded
                         engine runs. Same choice nam_to_cpp_array.py makes.
    WaveNet              taken directly, but almost certainly rejected: a plain
                         NAM WaveNet is a different, larger topology and will
                         not have 1,871 weights.

The weight count IS checked here, and that matters. nam_to_cpp_array.py emits
`float x[kA2WeightCount] = {...}` whatever it extracted; too many weights is a
compile error, but **too few is silently zero-padded** and produces a capture
that loads cleanly and sounds wrong. Better to fail at conversion with a clear
message.

Usage:
    python3 tools/nam_to_a2nb.py downloaded.nam
    python3 tools/nam_to_a2nb.py downloaded.nam --name "PLEXI" --gain 0.8
    python3 tools/nam_to_a2nb.py *.nam --out captures/
"""

import argparse
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from export_captures import EXPECTED_WEIGHTS, NAME_LEN, pack, verify  # noqa: E402


def extract(path):
    """Return (weights, note) or raise ValueError with something actionable."""
    with open(path) as f:
        d = json.load(f)

    arch = d.get("architecture", "")
    if arch == "SlimmableContainer":
        subs = d.get("config", {}).get("submodels")
        if not subs:
            raise ValueError("SlimmableContainer with no submodels")
        sub = min(subs, key=lambda s: abs(s.get("max_value", 1.0) - 0.5))
        model = sub["model"]
        note = f"A2, submodel max_value={sub.get('max_value')}"
    elif arch == "WaveNet":
        model = d
        note = "plain WaveNet"
    else:
        raise ValueError(
            f"unsupported architecture {arch!r}. The engine runs A2-Lite only — "
            f"look for the A2 badge, or filter by architecture when downloading.")

    weights = model.get("weights")
    if not isinstance(weights, list):
        raise ValueError("no flat 'weights' list in the model")
    return [float(w) for w in weights], note


def default_name(path):
    base = pathlib.Path(path).stem
    base = re.sub(r"^\[.*?\]\s*", "", base)          # drop [AMP] / [PRE] tags
    base = re.sub(r"[^A-Za-z0-9 _-]+", " ", base)
    base = re.sub(r"\s+", " ", base).strip()
    # Trim after truncating too, or "Test Plexi - DI" leaves a trailing
    # space in the header and on the panel.
    return base[: NAME_LEN - 1].strip() or "CAPTURE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nam", nargs="+", help=".nam file(s)")
    ap.add_argument("--out", default=None, help="output directory (default: alongside)")
    ap.add_argument("--name", help="display name; only valid with a single input")
    ap.add_argument("--gain", type=float, default=1.0,
                    help="output gain for loudness matching. NAM's own loudness "
                         "metadata proved unreliable across captures, so this is "
                         "set by ear (default 1.0)")
    args = ap.parse_args()

    if args.name and len(args.nam) > 1:
        raise SystemExit("--name only makes sense with one input file")

    ok = fail = 0
    for src in args.nam:
        src = pathlib.Path(src)
        try:
            weights, note = extract(src)
        except (ValueError, KeyError, json.JSONDecodeError) as e:
            print(f"  {src.name}: {e}")
            fail += 1
            continue

        if len(weights) != EXPECTED_WEIGHTS:
            print(f"  {src.name}: {len(weights)} weights, engine needs "
                  f"{EXPECTED_WEIGHTS} ({note}). Not an A2-Lite model.")
            fail += 1
            continue

        name = args.name or default_name(src)
        blob = pack(name, args.gain, weights)
        verify(blob, name, args.gain, weights)

        outdir = pathlib.Path(args.out) if args.out else src.parent
        outdir.mkdir(parents=True, exist_ok=True)
        safe = re.sub(r"[^A-Za-z0-9_-]", "_", name)
        dst = outdir / f"{safe}.a2nb"
        dst.write_bytes(blob)
        print(f"  {src.name} -> {dst.name}  ({note}, gain {args.gain})")
        ok += 1

    print(f"\n{ok} converted, {fail} rejected")
    if ok:
        print("Copy the .a2nb files to the root of a FAT32 card.")
    return 1 if fail and not ok else 0


if __name__ == "__main__":
    sys.exit(main())
