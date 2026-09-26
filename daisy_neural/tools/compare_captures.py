#!/usr/bin/env python3
"""
compare_captures.py — the same sound through every capture.

Runs one signal through all five captures at full precision and writes a WAV
each, so the differences that matter can be judged by ear rather than inferred.

Full precision is what the module actually does at the top of the depth knob:
kBitsMax is 16 and the firmware memcpys rather than quantising there, so these
files are exactly the module's output with the depth control off.

Each capture's outputGain is applied — bkshepherd's hand-tuned loudness match.
Without it the comparison is unfair, since a hotter capture reads as "better"
when it is only louder.

Usage:
    python3 tools/compare_captures.py
    python3 tools/compare_captures.py --input yours.wav
    python3 tools/compare_captures.py --signal pluck --peak 0.3
"""

import argparse
import array
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))

from export_captures import parse_header  # noqa: E402
from quantisation_study import (  # noqa: E402
    HEADER, make_pluck, make_sweep, make_synth, read_wav, run_engine,
    scale_peak, write_wav, metrics,
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--signal", choices=["synth", "pluck", "sweep"], default="synth")
    ap.add_argument("--input", help="16-bit mono WAV to use instead")
    ap.add_argument("--peak", type=float, default=0.5)
    ap.add_argument("--out", default=str(PROJECT / "amp_compare"))
    args = ap.parse_args()

    harness = HERE / "a2_host"
    if not harness.exists():
        raise SystemExit(
            f"harness not built. Run:\n"
            f"  c++ -std=c++17 -O2 -I{PROJECT} -o {harness} {HERE / 'a2_host.cpp'}")

    arrays, entries = parse_header(HEADER)
    if not entries:
        raise SystemExit("no captures found in the header")

    workdir = pathlib.Path(args.out)
    workdir.mkdir(parents=True, exist_ok=True)

    if args.input:
        audio = read_wav(args.input)
    elif args.signal == "sweep":
        audio = make_sweep()
    elif args.signal == "pluck":
        audio = make_pluck()
    else:
        audio = make_synth()
    scale_peak(audio, args.peak)
    write_wav(workdir / "in_dry.wav", audio)

    print(f"signal  {args.input or args.signal}, {len(audio)} samples, peak {args.peak}")
    print()
    print(f"  {'capture':<12} {'gain':>5} {'peak':>7} {'rms':>8}  {'vs 1st':>8}")

    first = None
    for name, symbol, gain in entries:
        weights = arrays.get(symbol)
        if not weights:
            print(f"  {name:<12} -- no array {symbol}")
            continue

        out = run_engine(weights, audio, workdir, symbol)
        # outputGain, as the firmware applies it.
        for i in range(len(out)):
            out[i] *= gain

        safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
        write_wav(workdir / f"amp_{safe}.wav", out)

        peak = max(abs(v) for v in out)
        rms = (sum(v * v for v in out) / len(out)) ** 0.5

        if first is None:
            first = out
            rel = "  (ref)"
        else:
            esr, _ = metrics(first, out)
            import math
            rel = f"{10 * math.log10(esr):>7.1f}dB" if esr > 0 else "      --"

        print(f"  {name:<12} {gain:>5.2f} {peak:>7.4f} {rms:>8.5f}  {rel}")

    print()
    print(f"files in {workdir}")
    print()
    print("The 'vs 1st' column is each capture's difference from the first, on the")
    print("same scale the quantisation study uses. It is there for context: if two")
    print("amps differ by less than a depth step does, the depth knob was never")
    print("going to be the interesting control.")


if __name__ == "__main__":
    main()
