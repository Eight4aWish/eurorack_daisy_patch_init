#!/usr/bin/env python3
"""
grnb_controls.py — set (or show) how Mirth's knobs drive a .grnb filter: the cutoff volts at
the FREQ knob's ends, and the Res dial's table (dial position -> the network's Res input
and the feedback of its output into its input). Writes the controls section (grnb.py) in
place, so an exported network does not have to be exported again.

    .venv/bin/python tools/grnb_controls.py f2_WASP_LP_16.grnb                     # show
    .venv/bin/python tools/grnb_controls.py f4_WASP_HP_16.grnb --cut=-10,0 \\
        --dial 0:0:0,7:7:0,9:12:0,10:12:0.15,11:12:0.5                          # set
    .venv/bin/python tools/grnb_controls.py f*.grnb --audio-scale 0.666 --gain 1.353

--audio-scale and --gain set the header's audio input scale and output gain (absolute
values). The level match of 2026-10-22: the networks were trained in the ES-10's units
(10.96 V out, 11.97 V in), the patch.init's input reaches full scale at 7.30 V and its
output at 8.85 V (tools/mirth_input_fs.py), so audio scale 7.30 / 10.96 = 0.666 and gain
11.97 / 8.85 = 1.353 make DRIVE at noon put volts in and get volts out as the module did.
"""

import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import grnb  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--cut", help="the network's cutoff volts at FREQ 0 and 10, e.g. 0,10 or (with =) --cut=-10,0")
    ap.add_argument("--dial", help="dial:res:feedback points, ascending, e.g. 0:0:0,10:10:0,11:10:0.5")
    ap.add_argument("--remove", action="store_true", help="remove the section (Mirth then uses the Wasp LP's)")
    ap.add_argument("--audio-scale", type=float, help="the header's scale on the audio input (absolute)")
    ap.add_argument("--gain", type=float, help="the header's output gain (absolute)")
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
        if args.audio_scale is not None or args.gain is not None:
            data = bytearray(data)
            name, gain, hidden, kinds, scales, _ = grnb.unpack(bytes(data))
            if args.gain is not None:
                struct.pack_into("<f", data, 12, args.gain)
            if args.audio_scale is not None:
                struct.pack_into("<f", data, 36 + 4 * kinds.index(grnb.KINDS["audio"]), args.audio_scale)
            data = bytes(data)  # the header is outside the payload's CRC
        if args.remove or args.cut or args.audio_scale is not None or args.gain is not None:
            grnb.unpack(data)
            f.write_bytes(data)
        _, gain, _, kinds, scales, _ = grnb.unpack(data)
        print(f"{f.name}: gain {gain:.3f}, audio scale {scales[kinds.index(grnb.KINDS['audio'])]:.3f}")
        c = grnb.unpack_controls(data)
        if c is None:
            print(f"{f.name}: no controls section (Mirth uses the Wasp LP's)")
        else:
            lo, hi, dial, res, fb = c
            print(f"{f.name}: cutoff {lo:g} to {hi:g} V; dial " + ", ".join(
                f"{d:g}->Res {r:g} fb {b:g}" for d, r, b in zip(dial, res, fb)))


if __name__ == "__main__":
    main()
