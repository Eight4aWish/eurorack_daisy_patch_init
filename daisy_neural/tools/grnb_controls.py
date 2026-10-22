#!/usr/bin/env python3
"""
grnb_controls.py — set (or show) how Mirth's knobs drive a .grnb filter: the cutoff volts at
the FREQ knob's ends, and the Res dial's table (dial position -> the network's Res input
and the feedback of its output into its input). Writes the controls section (grnb.py) in
place, so an exported network does not have to be exported again.

    .venv/bin/python tools/grnb_controls.py f2_WASP_LP_16.grnb                     # show
    .venv/bin/python tools/grnb_controls.py f4_WASP_HP_16.grnb --cut=-10,0 \\
        --dial 0:0:0,7:7:0,9:12:0,10:12:0.15,11:12:0.5                          # set
"""

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import grnb  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--cut", help="the network's cutoff volts at FREQ 0 and 10, e.g. 0,10 or (with =) --cut=-10,0")
    ap.add_argument("--dial", help="dial:res:feedback points, ascending, e.g. 0:0:0,10:10:0,11:10:0.5")
    ap.add_argument("--remove", action="store_true", help="remove the section (Mirth then uses the Wasp LP's)")
    args = ap.parse_args()
    for f in map(pathlib.Path, args.files):
        data = f.read_bytes()
        grnb.unpack(data)  # checks the network
        if args.remove:
            data = grnb.with_controls(data, None)
        elif args.cut or args.dial:
            if not (args.cut and args.dial):
                raise SystemExit("--cut and --dial go together")
            lo, hi = (float(v) for v in args.cut.split(","))
            pts = [tuple(float(v) for v in p.split(":")) for p in args.dial.split(",")]
            data = grnb.with_controls(data, grnb.pack_controls(lo, hi, *zip(*pts)))
        if args.remove or args.cut:
            grnb.unpack(data)
            f.write_bytes(data)
        c = grnb.unpack_controls(data)
        if c is None:
            print(f"{f.name}: no controls section (Mirth uses the Wasp LP's)")
        else:
            lo, hi, dial, res, fb = c
            print(f"{f.name}: cutoff {lo:g} to {hi:g} V; dial " + ", ".join(
                f"{d:g}->Res {r:g} fb {b:g}" for d, r, b in zip(dial, res, fb)))


if __name__ == "__main__":
    main()
