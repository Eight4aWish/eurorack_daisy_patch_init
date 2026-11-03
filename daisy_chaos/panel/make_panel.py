#!/usr/bin/env python3
"""
make_panel.py — Secret's faceplate for the Alchemy Lab, from Hermetic's own template.

Hermetic ships a KiCad front-panel template in the Alchemy SDK
(deps/alchemy-sdk/panel/front-panel-template.kicad_pcb, MIT) for exactly this: a firmware
author's own faceplate, made as a PCB. Hermetic's lettering is copper text with a
solder-mask opening over it, so it comes out as bare ENIG metal on black. Secret's is white
silkscreen instead, like the other Eight4aWish faceplates (David, 2026-11-03): each
placeholder's copper text becomes silkscreen in Secret's words, its mask opening goes, and
the module name and the maker are added the same way. Hermetic cut the holes and slots in
Edge.Cuts and have JLCPCB plate them with the board edges; here they are plated holes in the
drill file and the edges are left bare (see drilled()). Every hole position, copper disc,
LED window and rule is Hermetic's, so the template's MIT notice (© Hermetic Modular LLC) is
kept beside the outputs, in LICENSE-hermetic.txt.

Labels (daisy_chaos/src/secret.cpp, v1.1.0, on a working Alchemy Lab): knobs TUNE CHAOS /
CHAR TAME / AD SR; buttons MODEL and ENV, with B2 unlabelled (unused since v1.1.0; it was
TAME's mode, and B2 and B3 were swapped 2026-10-02 so each sits by its knobs); jacks V/OCT
CHAOS X over GATE TAME Y, the stock panel's CV1-CV6 taken as J3-J8; the audio-in column
unlabelled (Secret leaves J1/J2 unused); OUT as Hermetic labels it.

    python3 daisy_chaos/panel/make_panel.py            # writes secret_panel.kicad_pcb
    python3 daisy_chaos/panel/make_panel.py --gerbers  # and the JLCPCB zip + a preview

Order from JLCPCB like the other Eight4aWish faceplates: ENIG, black solder mask, white
silkscreen, 1.6 mm, Remove Mark. No edge plating and no order comment, which Hermetic's
panel/README.md asks for only because its holes are cut in the outline.
"""

import argparse
import math
import pathlib
import re
import shutil
import subprocess
import uuid
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
TEMPLATE = HERE.parent.parent / "deps" / "alchemy-sdk" / "panel" / "front-panel-template.kicad_pcb"
OUT = HERE / "secret_panel.kicad_pcb"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

KNOB_X = (69.16, 104.16)              # knob columns
KNOB_Y = (53.39, 81.40, 109.40)       # knob / button rows
KNOBS = {(0, 0): "TUNE", (1, 0): "CHAOS", (0, 1): "CHAR", (1, 1): "TAME", (0, 2): "AD", (1, 2): "SR"}
BUTTONS = {0: "MODEL", 1: "", 2: "ENV"}  # B2 unused since v1.1.0
JACK_X = (63.13, 74.92, 86.69, 98.42, 110.31)  # jack columns
JACKS = {  # (column, label row: 0 under the top jacks, 1 under the bottom ones)
    (1, 0): "V/OCT", (2, 0): "CHAOS", (3, 0): "X",
    (1, 1): "GATE", (2, 1): "TAME", (3, 1): "Y",
    (0, 1): "",        # Hermetic's "IN": Secret does not use the audio inputs
    (4, 1): "OUT",
}
TITLE = ("SECRET", 86.88, 39.3, 2.2)          # text, x, y (baseline), size: above Hermetic's top rule (y 40.2)
MAKER = ("Eight4aWish", 86.88, 158.6, 1.6)
RING = 0.25  # copper round each plated hole, mm: build123d kicad_faceplate.py's PLATED_RING


def blocks(s, head):
    """(start, end) of every balanced (head ...) block."""
    out, i = [], s.find(head)
    while i != -1:
        d = 0
        for j in range(i, len(s)):
            if s[j] == "(":
                d += 1
            elif s[j] == ")":
                d -= 1
                if d == 0:
                    out.append((i, j + 1))
                    break
        i = s.find(head, j)
    return out


def nearest(v, options):
    return min(range(len(options)), key=lambda k: abs(options[k] - v))


def label_for(text, x, y):
    """Secret's label for a template placeholder, and the x to centre it on."""
    if text == "KNOB LABEL":
        c, r = nearest(x, KNOB_X), nearest(y - 14.0, KNOB_Y)
        return KNOBS[(c, r)], KNOB_X[c]
    if text == "BUTTON LABEL":
        return BUTTONS[nearest(y - 10.9, KNOB_Y)], 86.64
    if text in ("JACK", "IN", "OUT"):
        row = 0 if y < 146 else 1
        col = {"IN": 0, "OUT": 4}.get(text, nearest(x + 2.8, JACK_X))  # template anchors are left edges
        return JACKS[(col, row)], JACK_X[col]
    return None


def retext(block, text, x, y=None, size=None, layer=None):
    """Same block with new text, centred on x (justify bottom), cache dropped so KiCad redraws."""
    b = re.sub(r'^\(gr_text "[^"]*"', f'(gr_text "{text}"', block)
    if layer:
        b = re.sub(r'\(layer "[^"]+"', f'(layer "{layer}"', b, count=1)
    b = re.sub(r"\(at ([\d.\-]+) ([\d.\-]+)( [\d.\-]+)?\)",
               lambda m: f"(at {x} {y if y is not None else m.group(2)} 0)", b, count=1)
    b = re.sub(r"\(justify[^)]*\)", "(justify bottom)", b)
    if size:
        b = re.sub(r"\(size [\d.]+ [\d.]+\)", f"(size {size} {size})", b)
        b = re.sub(r"\(thickness [\d.]+\)", f"(thickness {size * 0.25:.3f})", b)
    b = re.sub(r'\(uuid "[^"]*"\)', f'(uuid "{uuid.uuid4()}")', b)
    i = b.find("(render_cache")
    if i != -1:
        start, end = blocks(b, "(render_cache")[0]
        b = b[:start].rstrip() + "\n\t" + b[end:].lstrip()
    return b


def pt(block, key):
    return tuple(map(float, re.search(rf"\({key} ([\d.\-]+) ([\d.\-]+)\)", block).groups()))


def drilled(s):
    """The board with Hermetic's outline holes and edge marks removed, and a footprint of
    plated drill holes in their place.

    The template cuts every hole and slot in Edge.Cuts, and the order comment asks JLCPCB to
    plate them along with the board edges, which the unfilled copper boxes on and outside the
    outline mark. Edge plating costs more than the board (David, 2026-11-03: £58 with it,
    £18 without), so each hole and slot becomes a plated drill hole, as on the other
    Eight4aWish faceplates, and the edge marks go. Hermetic's copper discs and every mask
    opening stay, so the rings and LED windows look the same.
    """
    cut, pads, ends, marks = [], [], [], 0
    for start, end in blocks(s, "(gr_circle\n"):  # KiCad 9 opens a shape on a line of its own
        b = s[start:end]
        if '(layer "Edge.Cuts")' in b:
            (cx, cy), (ex, ey) = pt(b, "center"), pt(b, "end")
            d = 2 * math.hypot(ex - cx, ey - cy)
            pads.append(("circle", cx, cy, d, d))
            cut.append((start, end))
    for start, end in blocks(s, "(gr_arc\n") + blocks(s, "(gr_line\n"):
        b = s[start:end]
        if '(layer "Edge.Cuts")' not in b:
            continue
        cut.append((start, end))
        if b.startswith("(gr_arc"):  # a half circle, one end of a slot
            (sx, sy), (mx, my), (ex, ey) = pt(b, "start"), pt(b, "mid"), pt(b, "end")
            c, r = ((sx + ex) / 2, (sy + ey) / 2), math.hypot(ex - sx, ey - sy) / 2
            assert abs(math.hypot(mx - c[0], my - c[1]) - r) < 0.01
            ends.append((c, r))
    slots = 0
    while ends:
        a, r = ends.pop()
        b = ends.pop(min(range(len(ends)), key=lambda k: math.dist(a, ends[k][0])))[0]
        assert abs(a[1] - b[1]) < 0.05  # every slot here runs left to right
        pads.append(("oval", (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, math.dist(a, b) + 2 * r, 2 * r))
        slots += 1
    for start, end in blocks(s, "(gr_rect\n"):
        b = s[start:end]
        if re.search(r'\(layer "[FB]\.Cu"\)', b) and "(fill no)" in b:
            cut.append((start, end))
            marks += 1
    # 10 jacks, 6 knobs, 3 buttons; 4 rail slots and the USB slot; 9 edge marks on F.Cu, 1 on B.Cu
    assert (len(pads) - slots, slots, marks) == (19, 5, 10), (len(pads) - slots, slots, marks)
    for start, end in sorted(cut, reverse=True):
        s = s[:start] + s[end:]

    fp = [f'\t(footprint "secret:holes"\n\t\t(layer "F.Cu")\n\t\t(uuid "{uuid.uuid4()}")\n\t\t(at 0 0)']
    for name, value in (("Reference", "PANEL"), ("Value", "holes")):
        fp.append(f'\t\t(property "{name}" "{value}" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{uuid.uuid4()}")'
                  "\n\t\t\t(effects (font (size 1 1) (thickness 0.15))))")
    fp.append("\t\t(attr through_hole exclude_from_pos_files exclude_from_bom)")
    for shape, x, y, w, h in pads:
        drill = f"{w:.3f}" if shape == "circle" else f"oval {w:.3f} {h:.3f}"
        fp.append(f'\t\t(pad "" thru_hole {shape} (at {x:.4f} {y:.4f}) (size {w + 2 * RING:.3f} {h + 2 * RING:.3f})'
                  f' (drill {drill}) (layers "*.Cu" "*.Mask") (uuid "{uuid.uuid4()}"))')
    fp.append("\t)")
    return s, "\n".join(fp) + "\n", len(pads) - slots, slots, marks


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--gerbers", action="store_true", help="also plot the JLCPCB zip and a preview render")
    args = ap.parse_args()

    s = TEMPLATE.read_text()
    edits, done = [], {}
    pair = None
    for start, end in blocks(s, "(gr_text "):
        b = s[start:end]
        text = re.match(r'\(gr_text "([^"]*)"', b).group(1)
        x, y = map(float, re.search(r"\(at ([\d.\-]+) ([\d.\-]+)", b).groups())
        layer = re.search(r'\(layer "([^"]+)"', b).group(1)
        new = label_for(text, x, y)
        if new is None:
            continue
        label, cx = new
        # Each of Hermetic's labels is there twice, as copper text and as the mask opening
        # over it. The copper copy becomes white silkscreen; the mask copy goes.
        silk = bool(label) and layer == "F.Cu"
        edits.append((start, end, retext(b, label, cx, layer="F.SilkS") if silk else ""))
        done.setdefault(label or "(removed)", []).append("F.SilkS" if silk else f"{layer} removed")
        if text == "KNOB LABEL" and pair is None:
            pair = {}
        if text == "KNOB LABEL":
            pair.setdefault(layer, b)  # a template block per layer, for the new texts
    for start, end, new in sorted(edits, reverse=True):
        s = s[:start] + new + s[end:]
    s, extra, holes, slots, marks = drilled(s)

    # The module name and the maker, in white silkscreen like the labels.
    for txt, x, y, size in (TITLE, MAKER):
        extra += "\t" + retext(pair["F.Cu"], txt, x, y, size, layer="F.SilkS") + "\n"
    s = s.rstrip()
    assert s.endswith(")")
    s = s[:-1].rstrip() + "\n" + extra + ")\n"
    OUT.write_text(s)

    for k, v in sorted(done.items()):
        print(f"  {k:10s} {', '.join(sorted(v))}")
    print(f"wrote {OUT.name}: {len(edits)} labels set, title and maker added")
    print(f"  {holes} holes and {slots} slots plated in the drill file, {marks} edge-plating marks removed")

    if args.gerbers:
        g = HERE / "gerbers"
        shutil.rmtree(g, ignore_errors=True)
        g.mkdir()
        subprocess.run([KICAD_CLI, "pcb", "export", "gerbers", "--output", str(g) + "/", str(OUT)], check=True,
                       capture_output=True)
        subprocess.run([KICAD_CLI, "pcb", "export", "drill", "--output", str(g) + "/", "--format", "excellon",
                        "--excellon-separate-th", "--drill-origin", "absolute", "--excellon-units", "mm", str(OUT)],
                       check=True, capture_output=True)
        z = HERE / "secret_panel_gerbers.zip"
        with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in sorted(g.iterdir()):
                zf.write(f, f.name)
        shutil.rmtree(g)
        subprocess.run([KICAD_CLI, "pcb", "render", "--side", "top", "--width", "1000", "--height", "2100",
                        "--background", "opaque", "--quality", "high", "--output", str(HERE / "secret_panel_preview.png"),
                        str(OUT)], check=True, capture_output=True)
        print(f"wrote {z.name} and secret_panel_preview.png")


if __name__ == "__main__":
    main()
