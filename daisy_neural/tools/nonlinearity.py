#!/usr/bin/env python3
"""
nonlinearity.py — is it actually distorting, or just filtering?

A fair question about untrained weights: A2's only nonlinearity is LeakyReLU
and everything else is linear FIR, so a random network might never drive the
nonlinearity and end up a cascade of random filters wearing a neural hat.

Two tests settle it.

HOMOGENEITY. A linear system scales exactly: out(x/2) * 2 == out(x). Feed the
same signal at two levels and measure how far that fails. Reported in dB
relative to the output's own power, so around -40 dB is essentially linear and
0 dB means the deviation is as large as the signal.

HARMONICS. A pure sine into a linear filter comes back a pure sine, scaled and
phase-shifted. Harmonics mean genuine nonlinearity. Measured with Goertzel at
the first six multiples, reported as THD and as the individual ratios — because
WHICH harmonics dominate says more than how many there are:

    odd-dominant (h3 > h2)   symmetric clipping. Valve amps, square-ish, the
                             sound "distortion" usually means.
    even-dominant (h2 > h3)  asymmetric, rectifying. Warmer, octave-flavoured,
                             and the signature of an untrained LeakyReLU stack
                             whose asymmetry nothing has balanced out.

Usage:
    python3 tools/nonlinearity.py
"""

import array
import math
import pathlib
import random
import sys

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))

from export_captures import parse_header  # noqa: E402
from quantisation_study import HEADER, make_synth, run_engine, scale_peak  # noqa: E402
from random_weights import apply_tilt, dcblock, gauss_region, sanitise  # noqa: E402

WORK = PROJECT / "random_weights"


def homogeneity(w, tag):
    a = make_synth()
    scale_peak(a, 0.5)
    b = make_synth()
    scale_peak(b, 0.25)
    oa = dcblock(run_engine(w, a, WORK, tag + "_full"))
    ob = dcblock(run_engine(w, b, WORK, tag + "_half"))
    sanitise(oa)
    sanitise(ob)
    n = min(len(oa), len(ob))
    num = sum((oa[i] - 2 * ob[i]) ** 2 for i in range(n))
    den = sum(oa[i] * oa[i] for i in range(n)) or 1e-30
    return 10 * math.log10(num / den)


def goertzel(sig, f, sr=48000):
    w = 2 * math.pi * f / sr
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for x in sig:
        s0 = x + c * s1 - s2
        s2, s1 = s1, s0
    return math.hypot(s1 - s2 * math.cos(w), s2 * math.sin(w))


def harmonics(w, tag, f0=220.0, sr=48000, secs=1.0, amp=0.4):
    n = int(secs * sr)
    sig = array.array("f", [amp * math.sin(2 * math.pi * f0 * i / sr) for i in range(n)])
    out = dcblock(run_engine(w, sig, WORK, tag + "_sine"))
    sanitise(out)
    mags = [goertzel(out, f0 * k) for k in range(1, 7)]
    fund = mags[0] or 1e-30
    thd = math.sqrt(sum(m * m for m in mags[1:])) / fund
    return (20 * math.log10(thd) if thd > 0 else -999.0,
            [m / fund for m in mags[1:4]])


def main():
    arrays, _ = parse_header(HEADER)
    ref = arrays["kWeightsJcm800"]
    WORK.mkdir(parents=True, exist_ok=True)

    systems = [
        ("trained JCM800", ref),
        ("random seed 1", gauss_region(ref, random.Random(1))),
        ("random seed 12", gauss_region(ref, random.Random(12))),
        ("random s1 tilt .6", apply_tilt(gauss_region(ref, random.Random(1)), 0.6)),
    ]

    print(f"  {'system':<20} {'homogeneity':>12} {'THD':>8}   {'h2':>6} {'h3':>6} {'h4':>6}  character")
    for name, w in systems:
        tag = name.replace(" ", "_").replace(".", "")
        h = homogeneity(w, tag)
        thd, hs = harmonics(w, tag)
        char = "odd-dominant" if hs[1] > hs[0] else "EVEN-dominant"
        print(f"  {name:<20} {h:>10.1f}dB {thd:>7.1f}dB   "
              f"{hs[0]:>6.3f} {hs[1]:>6.3f} {hs[2]:>6.3f}  {char}")

    print()
    print("  homogeneity: -40 dB would be essentially linear. Everything here is")
    print("  far from that, so none of these is 'just a filter'.")


if __name__ == "__main__":
    main()
