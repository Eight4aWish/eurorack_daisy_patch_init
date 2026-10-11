#!/usr/bin/env python3
"""
level_captures.py — set each .a2nb's output gain so every capture on a card plays
equally loud.

Each capture is played through the real A2 engine (a2_host, built from the same
nam/nam_a2_runtime.h the firmware runs) on the not-amps' test phrase
(test_signal.py, peak 0.5), and its gain set so its A-WEIGHTED level hits one
target. Only the header's gain field changes; the CRC covers the weights, so
the file stays valid.

Why A-weighted, not RMS. The starter set was first levelled by plain RMS, and
on the module the Ampeg SVT sounded far quieter than everything else. The test
phrase's fundamentals sit at 98–165 Hz and the Ampeg puts 89% of its output
under 150 Hz, so RMS counted mostly bass the ear barely hears — and turned the
Ampeg down hardest. Measured A-weighted, that RMS-matched set spanned 10 dB.
K-weighting (LUFS) barely separated them, since it hardly touches 60–150 Hz.
A-weighting is still a model of hearing, not hearing: if one stands out on the
module, believe the module.

The default target, −27.0 dB, is where the 24 captures on the card landed on
2026-10-11 (the median), so levelling new captures to it matches that card.

Build the engine host once, from tools/ (it is gitignored):

    cd tools && c++ -std=c++17 -O2 -I.. -o a2_host a2_host.cpp

Usage (in the project .venv — numpy and scipy):

    .venv/bin/python tools/level_captures.py captures/starter/*.a2nb          # measure only
    .venv/bin/python tools/level_captures.py --write captures/card/*.a2nb     # level to −27.0
    .venv/bin/python tools/level_captures.py --write --target -26 new.a2nb
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from a2nb import HEADER_SIZE, unpack, with_gain  # noqa: E402
from test_signal import SAMPLE_RATE, make_synth, scale_peak  # noqa: E402

HOST = HERE / "a2_host"
TARGET_DB = -27.0


def a_weighting():
    """IEC 61672 A-weighting as second-order sections at the engine's rate."""
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    zeros = [0.0] * 4
    poles = ([-2 * np.pi * f4] * 2 + [-2 * np.pi * f3, -2 * np.pi * f2]
             + [-2 * np.pi * f1] * 2)
    k = (2 * np.pi * f4) ** 2 * 10 ** (1.9997 / 20)  # 0 dB at 1 kHz
    z, p, kd = signal.bilinear_zpk(zeros, poles, k, SAMPLE_RATE)
    return signal.zpk2sos(z, p, kd)


def render(blob, phrase, tmp):
    """The capture's output for the phrase, through the real engine, at its stored gain."""
    _, gain, _, _ = unpack(blob)
    weights, out = tmp / "w.f32", tmp / "out.f32"
    weights.write_bytes(blob[HEADER_SIZE:])
    subprocess.run([str(HOST), str(weights), str(phrase), str(out)], check=True,
                   capture_output=True)
    return np.fromfile(out, "<f4").astype(np.float64) * gain


def db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-20)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="+", help=".a2nb files")
    ap.add_argument("--target", type=float, default=TARGET_DB,
                    help=f"A-weighted level to set, dB (default {TARGET_DB})")
    ap.add_argument("--write", action="store_true", help="rewrite the gains (default: measure only)")
    args = ap.parse_args()

    if not HOST.exists():
        raise SystemExit(f"no {HOST.name}: build it first —\n"
                         f"  cd {HERE} && c++ -std=c++17 -O2 -I.. -o a2_host a2_host.cpp")

    sos = a_weighting()
    tmp = pathlib.Path(tempfile.mkdtemp())
    phrase = tmp / "phrase.f32"
    sig = make_synth()
    scale_peak(sig, 0.5)
    np.asarray(sig, "<f4").tofile(phrase)

    print(f"{'file':24} {'name':12} {'gain':>7} {'RMS':>7} {'A-wtd':>7}  {'vs target':>9}  <150 Hz")
    for path in map(pathlib.Path, args.files):
        blob = path.read_bytes()
        try:
            name, gain, _, crc_ok = unpack(blob)
        except ValueError as e:
            print(f"{path.name:24} {e}")
            continue
        if not crc_ok:
            print(f"{path.name:24} {name:12} bad CRC — skipped")
            continue
        y = render(blob, phrase, tmp)
        level = db(signal.sosfilt(sos, y))
        spec = np.abs(np.fft.rfft(y)) ** 2
        low = spec[np.fft.rfftfreq(len(y), 1 / SAMPLE_RATE) < 150].sum() / spec.sum()
        line = (f"{path.name:24} {name:12} {gain:7.3f} {db(y):7.1f} {level:7.1f}  "
                f"{level - args.target:+8.1f}   {low:5.0%}")
        if args.write:
            new_gain = gain * 10 ** ((args.target - level) / 20)
            path.write_bytes(with_gain(blob, new_gain))
            line += f"   gain -> {new_gain:.3f}"
        print(line)

    if not args.write:
        print("\nmeasure only; --write sets each gain to the target")


if __name__ == "__main__":
    main()
