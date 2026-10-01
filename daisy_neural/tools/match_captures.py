#!/usr/bin/env python3
"""
match_captures.py — which downloaded .nam is which compiled-in capture?

The five development captures came to this repo as C arrays in
nam/model_data_nam_a2.h (via bkshepherd), never as .nam files. TONE3000 hands
out packs as zips holding many .nam files each. This finds, for each of the
five, the downloaded file whose A2-Lite parameters are the same numbers — so a
match is proved by the network itself, not guessed from a file name, and a
capture its creator has since retrained shows up as a near miss, not a match.

Point it at the zips as downloaded, at folders, or at loose .nam files:

    python3 tools/match_captures.py ~/Downloads/*.zip
    python3 tools/match_captures.py ~/Downloads/*.zip --keep captures/nam/

--keep copies each matched .nam there under its own file name. Both the header
and any .nam are gitignored: TONE3000's T3K licence forbids redistributing them.
"""

import argparse
import json
import pathlib
import shutil
import sys
import tempfile
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from export_captures import HEADER, parse_header  # noqa: E402
from nam_to_a2nb import extract  # noqa: E402

# Same numbers to within float32 rounding. The header was printed from the same
# JSON doubles, so a true match is normally exact; this only absorbs rounding.
SAME = 1e-6
# Unrelated captures differ by whole units (typically 2–9 at the worst
# parameter). Anything this close is the same network, changed a little.
NEAR = 0.05


def nam_files(paths, tmp):
    """Yield (label, path_on_disk) for every .nam in the inputs, unzipping into tmp."""
    for p in map(pathlib.Path, paths):
        if p.is_dir():
            for f in sorted(p.rglob("*")):
                if f.suffix.lower() == ".nam":
                    yield str(f), f
                elif f.suffix.lower() == ".zip":
                    yield from from_zip(f, tmp)
        elif p.suffix.lower() == ".zip":
            yield from from_zip(p, tmp)
        elif p.suffix.lower() == ".nam":
            yield str(p), p


def from_zip(zpath, tmp):
    with zipfile.ZipFile(zpath) as z:
        for i, member in enumerate(z.namelist()):
            if member.lower().endswith(".nam") and not pathlib.Path(member).name.startswith("._"):
                out = tmp / f"{zpath.stem}_{i}.nam"
                out.write_bytes(z.read(member))
                yield f"{zpath.name}:{member}", out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inputs", nargs="+", help="zips, folders or .nam files")
    ap.add_argument("--keep", help="copy each matched .nam into this folder")
    args = ap.parse_args()

    if not HEADER.exists():
        raise SystemExit(f"{HEADER} not found — it is the reference, and lives on the working machine only")
    arrays, entries = parse_header(HEADER)

    best = {sym: (float("inf"), None, None) for _, sym, _ in entries}
    seen = skipped = 0
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        for label, path in nam_files(args.inputs, tmp):
            seen += 1
            try:
                weights, _ = extract(path)
            except (ValueError, KeyError, json.JSONDecodeError):
                skipped += 1  # not A2, or not a capture
                continue
            if len(weights) != len(next(iter(arrays.values()))):
                skipped += 1
                continue
            for _, sym, _ in entries:
                ref = arrays[sym]
                d = max(abs(a - b) for a, b in zip(weights, ref))
                if d < best[sym][0]:
                    keep = None
                    if args.keep:
                        keep = tmp / f"keep_{sym}.nam"
                        shutil.copyfile(path, keep)
                    best[sym] = (d, label, keep)

        print(f"{seen} .nam files read, {skipped} skipped (not A2-Lite)\n")
        for name, sym, _ in entries:
            d, label, keep = best[sym]
            if label is None:
                print(f"  {name:8}  no A2 file to compare")
                continue
            if d < SAME:
                print(f"  {name:8}  MATCH\n            {label}")
            elif d < NEAR:
                print(f"  {name:8}  NEAR MISS, max difference {d:.3g} — the same capture, retrained or "
                      f"re-exported since?\n            {label}")
            else:
                print(f"  {name:8}  not in these downloads")
            if keep and d < SAME:
                dest = pathlib.Path(args.keep)
                dest.mkdir(parents=True, exist_ok=True)
                target = dest / pathlib.Path(label.split(":")[-1]).name
                shutil.copyfile(keep, target)
                print(f"            kept as {target}")


if __name__ == "__main__":
    main()
