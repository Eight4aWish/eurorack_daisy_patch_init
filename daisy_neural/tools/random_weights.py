#!/usr/bin/env python3
"""
random_weights.py — what does the A2 architecture do untrained?

A trained capture is the architecture plus what it learned. This asks what the
architecture alone contributes: 23 dilated causal convolution layers, LeakyReLU,
a 16-tap head, with weights that were never trained on anything.

Why it is worth knowing. If untrained weights produce something musically
interesting, that is a sound source rather than an emulation — no target, no
fidelity question, no licensing question about somebody else's capture. Exactly
the Eurorack-native use the amp captures are not.

Five ways of not-training, because "random" has choices in it and they are not
equivalent:

    gauss_global  N(0, sigma) with sigma from a real capture. The naive version.
    gauss_region  sigma matched per region (conv / per-layer / head), since the
                  head's spread is less than half the conv layers' and getting
                  that wrong is the difference between output and silence.
    shuffle       a real capture's weights, permuted. Identical distribution,
                  no structure — isolates learned STRUCTURE from learned SCALE.
    sign_flip     real magnitudes, random signs. Keeps more structure again.
    uniform       U(-max, max). Worst case, included as a floor.

23 layers deep, a scale error compounds: slightly too small and the signal
vanishes through the stack, slightly too large and it saturates or blows up. The
measurements below say which happened, so read them before listening.

Usage:
    python3 tools/random_weights.py
    python3 tools/random_weights.py --seed 7 --peak 0.3
"""

import argparse
import array
import math
import pathlib
import random
import statistics
import sys

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))

from export_captures import parse_header  # noqa: E402
from quantisation_study import (  # noqa: E402
    HEADER, make_synth, read_wav, run_engine, scale_peak, write_wav,
)

# Regions of the flat weight array, from the runtime's own offsets:
# 3 rechannel, 1404 conv, 23*18 per-layer, 16*3 head, 2 tail.
REGIONS = [("rechannel", 0, 3), ("conv", 3, 1407),
           ("per-layer", 1407, 1821), ("head", 1821, 1869), ("tail", 1869, 1871)]


def gauss_global(ref, rng):
    s = statistics.pstdev(ref)
    return [rng.gauss(0.0, s) for _ in ref]


def gauss_region(ref, rng):
    out = list(ref)
    for _, a, b in REGIONS:
        seg = ref[a:b]
        s = statistics.pstdev(seg) if len(seg) > 1 else abs(seg[0])
        for i in range(a, b):
            out[i] = rng.gauss(0.0, s)
    return out


def shuffle(ref, rng):
    out = list(ref)
    rng.shuffle(out)
    return out


def sign_flip(ref, rng):
    return [abs(w) * (1 if rng.random() < 0.5 else -1) for w in ref]


def uniform(ref, rng):
    m = max(abs(w) for w in ref)
    return [rng.uniform(-m, m) for _ in ref]


MAKERS = [("gauss_global", gauss_global), ("gauss_region", gauss_region),
          ("shuffle", shuffle), ("sign_flip", sign_flip), ("uniform", uniform)]


def dcblock(sig, sr=48000, fc=20.0):
    """One-pole high pass — the same thing the module's output stage should do.

    This is not optional for untrained weights. A trained capture settles near
    zero mean because training put it there; an untrained one has nothing
    pulling it to centre, so it parks on a large DC offset with the audio
    riding on top. Measured: the raw output can be RMS 20.4 of which 20.4 is
    DC. Block it and there is a real signal underneath.
    """
    r = math.exp(-2.0 * math.pi * fc / sr)
    y = array.array("f", bytes(4 * len(sig)))
    x1 = y1 = 0.0
    for i, x in enumerate(sig):
        y1 = x - x1 + r * y1
        x1 = x
        y[i] = y1
    return y


def sanitise(sig):
    """Untrained weights can produce NaN or inf. Report rather than write
    garbage into a WAV and blame the speakers."""
    bad = 0
    for i, v in enumerate(sig):
        if not math.isfinite(v):
            sig[i] = 0.0
            bad += 1
    return bad


def describe(sig, sr=48000):
    n = len(sig) or 1
    peak = max((abs(v) for v in sig), default=0.0)
    rms = math.sqrt(sum(v * v for v in sig) / n)
    dc = sum(sig) / n
    zc = sum(1 for i in range(1, n) if (sig[i - 1] < 0) != (sig[i] < 0))
    return peak, rms, dc, zc * sr / n / 2.0  # zero crossings -> rough Hz


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", default="kWeightsJcm800")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--peak", type=float, default=0.5)
    ap.add_argument("--input", help="16-bit mono WAV instead of the synth sequence")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="multiply the matched sigma. 1.0 is matched to a "
                         "trained capture and is the only usable value: 23 "
                         "layers compound, so 0.5 vanishes (output RMS drops "
                         "13x per halving) and 1.5 explodes (peak 1429)")
    ap.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3],
                    help="each seed is a different untrained network")
    ap.add_argument("--out", default=str(PROJECT / "random_weights"))
    args = ap.parse_args()

    if not (HERE / "a2_host").exists():
        raise SystemExit(f"harness not built:\n  c++ -std=c++17 -O2 -I{PROJECT} "
                         f"-o {HERE / 'a2_host'} {HERE / 'a2_host.cpp'}")

    arrays, _ = parse_header(HEADER)
    ref = arrays.get(args.capture)
    if not ref:
        raise SystemExit(f"no weights named {args.capture}")

    workdir = pathlib.Path(args.out)
    workdir.mkdir(parents=True, exist_ok=True)

    audio = read_wav(args.input) if args.input else make_synth()
    scale_peak(audio, args.peak)
    write_wav(workdir / "in_dry.wav", audio)

    print(f"reference  {args.capture}, seed {args.seed}, input peak {args.peak}")
    print()
    print(f"  {'weights':<14} {'peak':>7} {'rms':>8} {'gain':>7} {'dc':>8} "
          f"{'zc Hz':>7}  note")

    ipk, irms, _, izc = describe(audio)

    trained = dcblock(run_engine(ref, audio, workdir, "trained"))
    write_wav(workdir / "out_trained.wav", trained)
    pk, rms, dc, zc = describe(trained)
    print(f"  {'(trained)':<14} {pk:>7.4f} {rms:>8.5f} {rms/irms:>7.2f} "
          f"{dc:>+8.5f} {zc:>7.0f}  the real capture")

    # Every maker at one seed, to show how they differ...
    rng = random.Random(args.seed)
    for name, make in MAKERS:
        w = make(ref, rng)
        out = dcblock(run_engine(w, audio, workdir, name))
        bad = sanitise(out)
        pk, rms, dc, zc = describe(out)
        note = (f"{bad} non-finite" if bad else
                "silent" if pk < 1e-4 else
                "exploded" if pk > 10 else "alive")
        write_wav(workdir / f"out_{name}.wav", out)
        print(f"  {name:<14} {pk:>7.4f} {rms:>8.5f} {rms/irms:>7.2f} "
              f"{dc:>+8.5f} {zc:>7.0f}  {note}")

    # ...then the region-matched one across seeds, which is the interesting
    # axis: each seed is a different nonlinearity that never existed.
    print()
    for seed in args.seeds:
        rng = random.Random(seed)
        w = gauss_region(ref, rng)
        if args.scale != 1.0:
            w = [x * args.scale for x in w]
        out = dcblock(run_engine(w, audio, workdir, f"seed{seed}"))
        sanitise(out)
        pk, rms, dc, zc = describe(out)
        # Normalised for listening; levels vary per seed and would otherwise
        # dominate the comparison.
        m = max((abs(v) for v in out), default=1.0) or 1.0
        write_wav(workdir / f"out_seed{seed}.wav",
                  array.array("f", [v * 0.7 / m for v in out]))
        print(f"  {'seed ' + str(seed):<14} {pk:>7.4f} {rms:>8.5f} {rms/irms:>7.2f} "
              f"{dc:>+8.5f} {zc:>7.0f}  normalised for listening")

    print(f"\n  {'(dry input)':<14} {ipk:>7.4f} {irms:>8.5f} {1.0:>7.2f} "
          f"{'':>8} {izc:>7.0f}")
    print(f"\nfiles in {workdir}")
    print()
    print("zc Hz is zero crossings per second — a crude brightness proxy, not a")
    print("pitch. Much higher than the input means added harmonics or noise;")
    print("much lower means the stack has low-passed it into mush.")


if __name__ == "__main__":
    main()
