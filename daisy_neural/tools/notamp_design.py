#!/usr/bin/env python3
"""
notamp_design.py — the twelve not-amps, checked, measured, and written to src/notamps.h.

Chosen 2026-10-02 by tools/notamp_search.py (descriptors + CLAP, against 236 real
captures) and approved by ear; the README's "The measured search" has the numbers.
They replace the nine of 2026-09-29.

For each not-amp this:
  1. renders it through the firmware's own processor (tools/a2_notamp, which runs
     src/notamp_dsp.h) and through the search harness it was chosen with
     (tools/a2_explore), and checks they agree — bit for bit, except RATE, which the
     firmware streams with 48·R samples of latency, so it is compared shifted;
  2. measures its output level at nine steer positions, relative to its source
     capture played plainly, through the firmware processor;
  3. writes the definitions and the inverse level tables to src/notamps.h, so the
     steer changes the sound rather than the volume.

Needs the starter captures in captures/starter/ (CC0 / CC-BY, shipped with Mirth).

    c++ -std=c++17 -O2 -I. -Isrc -o tools/a2_notamp tools/a2_notamp.cpp
    c++ -std=c++17 -O2 -DNAM_A2_EXPLORE -I. -o tools/a2_explore_s1 tools/a2_explore.cpp
    .venv/bin/python tools/notamp_design.py
"""

import pathlib
import subprocess
import sys
import tempfile

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))
from quantisation_study import make_synth, scale_peak  # noqa: E402

STARTER = PROJECT / "captures" / "starter"
HEADER = PROJECT / "src" / "notamps.h"

# name (≤10 chars, the OLED), kind, cap_a, cap_b, lo, hi, log, layer, fixed
TWELVE = [
    ("SINE PLX",   "sine",    "PLEXI LORE", None,        0.3,  8.0,  True,  0,  0.0),
    ("SINE BUG",   "sine",    "BUGERA G5",  None,        0.3,  8.0,  True,  0,  0.0),
    ("LINEAR TR",  "slope",   "TWO ROCK",   None,        0.01, 1.0,  False, 0,  0.0),
    ("FB100 F57",  "fbgain",  "FENDER 57",  None,        0.0,  0.95, False, 0,  480.0),
    ("FB PCH PLX", "fbpitch", "PLEXI LORE", None,        48.0, 1200.0, True, 0, 0.8),
    ("FB PCH KAY", "fbpitch", "KAY 703",    None,        48.0, 1200.0, True, 0, 0.8),
    ("FRZ E TR",   "freeze",  "TWO ROCK",   None,        1.0,  1024.0, True, 3, 0.0),
    ("FRZ M F57",  "freeze",  "FENDER 57",  None,        1.0,  1024.0, True, 11, 0.0),
    ("FRZ M KAY",  "freeze",  "KAY 703",    None,        1.0,  1024.0, True, 11, 0.0),
    ("RATE SVT",   "rate",    "SVT-2 PRO",  None,        1.0,  6.0,  False, 0,  0.0),
    ("RATE BUG",   "rate",    "BUGERA G5",  None,        1.0,  6.0,  False, 0,  0.0),
    ("PAST BLU",   "morph",   "BUGERA G5",  "BLUESBRKR", 1.0,  1.3,  False, 0,  0.0),
]
RATE_STEPS = [1, 2, 3, 4, 6]
KIND_ENUM = {"sine": "Sine", "slope": "Slope", "freeze": "Freeze", "morph": "Morph",
             "fbgain": "FbGain", "fbpitch": "FbPitch", "rate": "Rate"}


def param(t, u):
    _, kind, _, _, lo, hi, log, _, _ = t
    if kind == "rate":
        return RATE_STEPS[int(round(u * 4))]
    return float(np.exp(np.log(lo) + (np.log(hi) - np.log(lo)) * u)) if log else lo + (hi - lo) * u


def explore_spec(t, v):
    _, kind, _, _, _, _, _, layer, fixed = t
    return {"sine": f"sine:{v}:{v}", "slope": f"slope:{v}:{v}", "freeze": f"freeze:{layer}:{v}:{v}",
            "morph": f"morph:{v}:{v}", "fbgain": f"fbgain:{int(fixed)}:{v}:{v}",
            "fbpitch": f"fbdelay:{fixed}:{v}:{v}", "rate": f"rate:{int(v)}"}[kind]


def main():
    caps = {}
    for f in sorted(STARTER.glob("*.a2nb")):
        b = f.read_bytes()
        caps[b[16:28].split(b"\0")[0].decode()] = (b[32:], np.frombuffer(b[12:16], "<f4")[0])
    tmp = pathlib.Path(tempfile.mkdtemp())
    a = make_synth()
    scale_peak(a, 0.5)
    sig = np.asarray(a, "<f4")
    (tmp / "in.f32").write_bytes(sig.tobytes())
    for n, (w, _) in caps.items():
        (tmp / f"{n}.f32").write_bytes(w)
    wpath = lambda n: str(tmp / f"{n}.f32")

    def run(cmd):
        subprocess.run(cmd, check=True, capture_output=True)
        return np.fromfile(tmp / "out.f32", "<f4").astype(np.float64)

    def notamp(t, u):
        name, kind, ca, cb, lo, hi, log, layer, fixed = t
        return run([str(HERE / "a2_notamp"), wpath(ca), wpath(cb) if cb else "-", str(tmp / "in.f32"),
                    str(tmp / "out.f32"), kind, str(lo), str(hi), "1" if log else "0", str(layer),
                    str(fixed), str(u), str(u)])

    def explore(t, u):
        _, _, ca, cb, *_ = t
        return run([str(HERE / "a2_explore_s1"), wpath(ca), wpath(cb) if cb else "-", str(tmp / "in.f32"),
                    str(tmp / "out.f32"), explore_spec(t, param(t, u))])

    def plain(n):
        return run([str(HERE / "a2_explore_s1"), wpath(n), "-", str(tmp / "in.f32"), str(tmp / "out.f32"), "none"])

    rms = lambda y: float(np.sqrt(np.mean(y[4800:] ** 2)))  # past the first 0.1 s
    defs = []
    print("not-amp       firmware vs harness              level at steer 0 … 1 (dB, before correction)")
    for t in TWELVE:
        name, kind, ca, cb, lo, hi, log, layer, fixed = t
        # 1. the firmware processor against the harness it was chosen with
        worst = -999.0
        for u in (0.0, 0.5, 1.0):
            f, e = notamp(t, u), explore(t, u)
            if kind == "rate":
                lag = 48 * param(t, u) if param(t, u) > 1 else 0
                f = f[lag:]  # the firmware's output trails by 48·R samples
            n = min(len(f), len(e))  # the harness pads its input to a multiple of 48·R
            f, e = f[:n], e[:n]
            if kind == "morph":  # the harness starts from A and rewrites; skip its first block's transient
                f, e = f[4800:], e[4800:]
            diff = np.sum((f - e) ** 2) / (np.sum(e ** 2) + 1e-30)
            worst = max(worst, 10 * np.log10(diff + 1e-30))
        agree = "bit-identical" if worst < -290 else f"worst {worst:6.1f} dB"
        # 2. level against the source capture played plainly
        ref = rms(plain(cb or ca))
        lv = [20 * np.log10(rms(notamp(t, k / 8)) / ref) for k in range(9)]
        print(f"  {name:10s}  {agree:22s}  " + " ".join(f"{x:+5.1f}" for x in lv))
        if worst > -60:
            raise SystemExit(f"{name}: the firmware processor does not match the harness ({worst:.1f} dB)")
        defs.append((t, [-x for x in lv]))

    # 3. the header
    L = ["// GENERATED by tools/notamp_design.py — do not edit by hand; rerun it.",
         "// The twelve not-amps (2026-10-02): a starter capture with one transform and one",
         "// steer, each measured outside the amp region by two judges and approved by ear.",
         "// Types and processing: notamp_dsp.h. gain_db is the inverse of the measured level,",
         "// so the steer changes the sound rather than the volume.",
         "#pragma once", "", '#include "notamp_dsp.h"', "", "namespace notamps", "{",
         "constexpr Def kDefs[] = {"]
    for (name, kind, ca, cb, lo, hi, log, layer, fixed), g in defs:
        cap_b = f'"{cb}"' if cb else "nullptr"
        ref = cb or ca
        L.append(f'    {{"{name}", Kind::{KIND_ENUM[kind]}, "{ca}", {cap_b}, "{ref}", {lo}f, {hi}f, '
                 f'{"true" if log else "false"}, {layer}, {fixed}f,')
        L.append("     {" + ", ".join(f"{x:.2f}f" for x in g) + "}},")
    L += ["};", "constexpr int kCount = (int)(sizeof(kDefs) / sizeof(kDefs[0]));", "} // namespace notamps", ""]
    HEADER.write_text("\n".join(L))
    print(f"\nwrote {HEADER.relative_to(PROJECT)}: {len(defs)} not-amps")


if __name__ == "__main__":
    main()
