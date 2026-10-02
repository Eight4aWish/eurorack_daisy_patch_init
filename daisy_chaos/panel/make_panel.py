#!/usr/bin/env python3
"""
make_panel.py — Secret's faceplate for the Alchemy Lab, from Hermetic's own template.

Hermetic ships a KiCad front-panel template in the Alchemy SDK
(deps/alchemy-sdk/panel/front-panel-template.kicad_pcb, MIT) for exactly this: a firmware
author's own faceplate, made as a PCB — copper text and a solder-mask opening over it, so
the lettering comes out as bare ENIG metal on black. This copies the template unchanged
except for its placeholder text, which it fills with Secret's labels by position, then adds
the module name and the maker. Every hole, ring window, plating note and tooling tab is
Hermetic's, so the template's MIT notice (© Hermetic Modular LLC) is kept beside the
outputs, in LICENSE-hermetic.txt.

Labels (daisy_chaos/src/secret.cpp): knobs TUNE CHAOS / CHAR TAME / AD SR; buttons MODEL,
TAME MODE, ENV (B2 and B3 swapped 2026-10-02 so each sits by its knobs); jacks V/OCT CHAOS X
over GATE TAME Y, the stock panel's CV1-CV6 taken as J3-J8; the audio-in column unlabelled
(Secret leaves J1/J2 unused); OUT as Hermetic labels it.

    python3 daisy_chaos/panel/make_panel.py            # writes secret_panel.kicad_pcb
    python3 daisy_chaos/panel/make_panel.py --gerbers  # and the JLCPCB zip + a preview

Order from JLCPCB as Hermetic's panel/README.md says: ENIG, edge plating, black solder
mask, standard FR4 and thickness, with the order comment it gives.
"""

import argparse
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
BUTTONS = {0: "MODEL", 1: "TAME MODE", 2: "ENV"}
JACK_X = (63.13, 74.92, 86.69, 98.42, 110.31)  # jack columns
JACKS = {  # (column, label row: 0 under the top jacks, 1 under the bottom ones)
    (1, 0): "V/OCT", (2, 0): "CHAOS", (3, 0): "X",
    (1, 1): "GATE", (2, 1): "TAME", (3, 1): "Y",
    (0, 1): "",        # Hermetic's "IN": Secret does not use the audio inputs
    (4, 1): "OUT",
}
TITLE = ("SECRET", 86.88, 39.3, 2.2)          # text, x, y (baseline), size: above Hermetic's top rule (y 40.2)
MAKER = ("Eight4aWish", 86.88, 158.6, 1.6)


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


def retext(block, text, x, y=None, size=None):
    """Same block with new text, centred on x (justify bottom), cache dropped so KiCad redraws."""
    b = re.sub(r'^\(gr_text "[^"]*"', f'(gr_text "{text}"', block)
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
        edits.append((start, end, "" if not label else retext(b, label, cx)))
        done.setdefault(label or "(removed)", []).append(layer)
        if text == "KNOB LABEL" and pair is None:
            pair = {}
        if text == "KNOB LABEL":
            pair.setdefault(layer, b)  # a template block per layer, for the new texts
    for start, end, new in sorted(edits, reverse=True):
        s = s[:start] + new + s[end:]

    # The module name and the maker, in the same copper + mask-opening pair.
    extra = ""
    for txt, x, y, size in (TITLE, MAKER):
        for layer in ("F.Cu", "F.Mask"):
            extra += "\t" + retext(pair[layer], txt, x, y, size) + "\n"
    s = s.rstrip()
    assert s.endswith(")")
    s = s[:-1].rstrip() + "\n" + extra + ")\n"
    OUT.write_text(s)

    for k, v in sorted(done.items()):
        print(f"  {k:10s} {', '.join(sorted(v))}")
    print(f"wrote {OUT.name}: {len(edits)} labels set, title and maker added")

    if args.gerbers:
        g = HERE / "gerbers"
        shutil.rmtree(g, ignore_errors=True)
        g.mkdir()
        subprocess.run([KICAD_CLI, "pcb", "export", "gerbers", "--output", str(g) + "/", str(OUT)], check=True,
                       capture_output=True)
        subprocess.run([KICAD_CLI, "pcb", "export", "drill", "--output", str(g) + "/", str(OUT)], check=True,
                       capture_output=True)
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
