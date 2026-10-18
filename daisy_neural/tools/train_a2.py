#!/usr/bin/env python3
"""
train_a2.py — train NAM A2 captures from wasp_capture.py's snapshot takes without NAM's
GUI, so a whole series can run unattended. Run it in the trainer's own environment
(.venv-nam, see WASP.md):

    .venv-nam/bin/python tools/train_a2.py captures/wasp/lp-res6/nam/snap3_+5.00V_lphp.wav
    .venv-nam/bin/python tools/train_a2.py captures/wasp/lp-res6 captures/wasp/hp-res6
    .venv-nam/bin/python tools/train_a2.py --ignore-checks captures/wasp/hp-res6/nam/snap5_-1.00V_lphp.wav

A session folder means every file in its nam/ (add --outputs lphp to skip BP). It does
what NAM's GUI does: nam.train.core.train, NAM's simplified trainer, which always builds
A2, then an export to .nam with the capture's metadata. Captures land in
<session>/a2/<take>.nam, NAM's working files in <session>/a2/_train/<take>/.

NAM's data checks stay on unless --ignore-checks. WASP.md says when that is justified:
an output so close to the Wasp's hiss that the hiss alone fails the check.
"""

import argparse
import json
import math
import os
import pathlib
import re
import time

os.environ.setdefault("MPLBACKEND", "Agg")  # NAM's plots go to files, never a window

from nam.models.metadata import GearType, UserMetadata  # noqa: E402
from nam.train import core  # noqa: E402
from nam.train.metadata import TRAINING_KEY  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
INPUT_WAV = HERE.parent / "captures" / "wasp" / "input.wav"
OUTPUT_NAMES = {"lphp": None, "bp": "BP"}  # None: LP or HP, from the session's Mix


def dbu(volts_per_full_scale):
    """NAM's calibration: the level of a full-scale sine, in dBu (0 dBu = 0.7746 V rms)."""
    return 20 * math.log10(volts_per_full_scale / math.sqrt(2) / 0.7746)


def metadata(session, take, out):
    mode = OUTPUT_NAMES[out] or session.get("mix", "").upper()
    cv = re.search(r"_([+-][\d.]+)V$", take)
    cv = float(cv.group(1)) if cv else None
    res = session.get("res")
    name = f"Wasp {mode} R{res:g} {cv:+g}V" if cv is not None else f"Wasp {mode} R{res:g}"
    return UserMetadata(
        name=name,
        modeled_by="Eight4aWish",
        gear_type=GearType.PEDAL,
        gear_make="Doepfer",
        gear_model=f"A-124 Wasp ({mode} out, Res {res:g}, CV 1 {cv:+g} V, {session.get('knobs', '')})",
        input_level_dbu=round(dbu(session["out_vfs"]), 2) if session.get("out_vfs") else None,
        output_level_dbu=round(dbu(session["in_vfs"]), 2) if session.get("in_vfs") else None,
    )


def jobs(paths, outputs):
    for p in map(pathlib.Path, paths):
        files = sorted((p / "nam").glob("*.wav")) if p.is_dir() else [p]
        for f in files:
            take, out = f.stem.rsplit("_", 1)
            if out in outputs:
                yield f.parent.parent, take, out, f


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("paths", nargs="+", help="nam/ WAVs, or session folders")
    ap.add_argument("--epochs", type=int, default=100, help="as NAM's GUI with a GPU (default 100)")
    ap.add_argument("--outputs", default="lphp,bp", help="which outputs, from a session folder")
    ap.add_argument("--ignore-checks", action="store_true", help="train even if NAM's data checks fail")
    ap.add_argument("--fast-dev-run", action="store_true", help="one batch only, to test the pipeline")
    ap.add_argument("--overwrite", action="store_true")
    args = ap.parse_args()

    for sdir, take, out, wav in jobs(args.paths, args.outputs.split(",")):
        session = json.loads((sdir / "session.json").read_text())
        basename = f"{take}_{out}"
        outdir = sdir / "a2"
        if (outdir / f"{basename}.nam").exists() and not args.overwrite and not args.fast_dev_run:
            print(f"{sdir.name}/{basename}: already trained, skipped")
            continue
        train_path = outdir / "_train" / basename
        train_path.mkdir(parents=True, exist_ok=True)
        user = metadata(session, take, out)
        print(f"{sdir.name}/{basename}: training '{user.name}', {args.epochs} epochs ...", flush=True)
        t0 = time.monotonic()
        result = core.train(
            str(INPUT_WAV), str(wav), str(train_path),
            epochs=args.epochs, silent=True, save_plot=True, modelname=basename,
            ignore_checks=args.ignore_checks, local=False, user_metadata=user,
            fast_dev_run=1 if args.fast_dev_run else False,
        )
        minutes = (time.monotonic() - t0) / 60
        if result is None or result.model is None:
            print(f"  not trained ({minutes:.1f} min): NAM's checks failed or training stopped; "
                  "see its messages above")
            continue
        if args.fast_dev_run:
            print(f"  pipeline works ({minutes:.1f} min); nothing exported")
            continue
        result.model.net.export(str(outdir), basename=basename, user_metadata=user,
                                other_metadata={TRAINING_KEY: result.metadata.model_dump()})
        val = result.metadata.validation_esr if hasattr(result.metadata, "validation_esr") else None
        print(f"  done in {minutes:.1f} min -> {outdir.name}/{basename}.nam"
              + (f", validation ESR {val:.4f}" if val is not None else ""), flush=True)


if __name__ == "__main__":
    main()
