#!/usr/bin/env python3
"""
filter_osc_check.py — whether a FILTERS network, its Res input held at its dial's top and
its output fed back into its input, makes a tone of its own: in the gaps between the test
phrase's notes (the input silent), across a cutoff sweep, per feedback amount. For the
over-Res part of a filter's dial (10 to 11), where the Wasp LP's feedback did this
(tools/notamp_spectra.py).

    .venv-nam/bin/python tools/filter_osc_check.py captures/wasp/gru_runs/hp_cr_gru16_s3 11.5 -10 0
                                                   (run folder, Res input, cutoff from, to)
"""

import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from filter_dial_calibrate import load, run  # noqa: E402
from notamp_ideas import phrase  # noqa: E402
from swords_glide import peak_hz  # noqa: E402

SR = 48000


def main():
    net = load(pathlib.Path(sys.argv[1]))
    res, lo, hi = (float(v) for v in sys.argv[2:5])
    xp = phrase()
    n = len(xp)
    cut = np.linspace(lo, hi, n)
    for fb in (0.0, 0.1, 0.2, 0.3, 0.5):
        y = run(net, xp, cut, np.full(n, res), fb)
        notes = np.sqrt(np.mean(y[np.abs(xp) > 0.05] ** 2))
        cells = []
        for k in range(16):
            a, b = int((k * 0.5 + 0.425) * SR), int((k * 0.5 + 0.495) * SR)
            lvl = 20 * np.log10(np.sqrt(np.mean(y[a:b] ** 2)) / notes + 1e-12)
            if lvl > -15:
                f0, _ = peak_hz(y[a:b], 25, 20000)
                cells.append(f"{cut[(a + b) // 2]:+.1f}V {f0:.0f}Hz ({lvl:+.0f}dB)")
        print(f"feedback {fb}: own tone at " + (", ".join(cells) if cells else "none"), flush=True)


if __name__ == "__main__":
    main()
