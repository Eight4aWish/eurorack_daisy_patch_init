"""Emit a bank as a drop-in replacement for Grids' own resources.cc node tables.

Sorrow's banks and Mutable Grids' map are the same data structure: 25 nodes of
96 bytes (3 instruments x 32 steps), interpolated bilinearly between the four
corners of a 5x5 grid. So a bank derived here drops straight into real Grids
hardware as a *data* substitution - byte for byte the same size, no code change,
no flash budget to worry about.

    python3 emit_resources.py club       > nodes_club.cc
    python3 emit_resources.py groove     > nodes_groove.cc
    python3 emit_resources.py jazzlatin  > nodes_jazzlatin.cc

Paste the output over the `node_0` ... `node_24` block in grids/resources.cc,
rebuild, and encode the .hex with Mutable's own
avr_audio_bootloader/fsk/encoder.py.

THE PERMUTATION MATTERS. Grids' drum_map[5][5] does not list its nodes in order:

    { node_10, node_8,  node_0,  node_9,  node_11 },
    { node_15, node_7,  node_13, node_12, node_6  },
    ...

Emilie arranged the 25 hand-authored patterns by ear so that neighbours are
musically related - measurably so: her arrangement is 14.8% more similar
neighbour-to-neighbour than random pairs, against 5.2% for the same patterns in
file order. Our banks come off a self-organising map, which does that arranging
automatically, so they are already row-major correct. To leave Grids' source
otherwise untouched, this inverts her table: node_k is written with whatever
belongs in the cell where Grids looks up node_k.
"""
import re, sys, pathlib

SRC = pathlib.Path(__file__).resolve().parents[2] / 'src'
BANKS = {
    'club':      ('grids_nodes_club.cpp',      'club_node_'),
    'groove':    ('grids_nodes_groove.cpp',    'groove_node_'),
    'jazzlatin': ('grids_nodes_jazzlatin.cpp', 'jazzlatin_node_'),
    # 'original' is Grids' own data, already in Grids' node-index space, so it is
    # emitted unpermuted. Its only use is as a self-test: the output should be
    # byte-identical to the node block in grids/resources.cc.
    'original':  ('grids_nodes.cpp',           'node_'),
}
# grids/pattern_generator.cc, drum_map[5][5], read row-major.
GRIDS_ORDER = [10, 8, 0, 9, 11, 15, 7, 13, 12, 6, 18, 14, 4, 5, 3,
               23, 16, 21, 1, 2, 24, 19, 17, 20, 22]


def load(bank):
    fn, prefix = BANKS[bank]
    txt = (SRC / fn).read_text()
    nodes = {}
    for m in re.finditer(rf'const uint8_t {prefix}(\d+)\[96\]\s*=\s*\{{(.*?)\}};', txt, re.S):
        vals = [int(v) for v in re.findall(r'\d+', m.group(2))]
        if len(vals) != 96:
            raise SystemExit(f"{fn}: {prefix}{m.group(1)} has {len(vals)} bytes, expected 96")
        nodes[int(m.group(1))] = vals
    if len(nodes) != 25:
        raise SystemExit(f"{fn}: found {len(nodes)} nodes, expected 25")
    return [nodes[i] for i in range(25)]


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in BANKS:
        raise SystemExit(f"usage: emit_resources.py [{'|'.join(BANKS)}]")
    bank = sys.argv[1]
    ours = load(bank)

    if bank == 'original':
        out = ours                     # already in Grids' node-index space
    else:
        # Cell p (row-major) must hold ours[p]; Grids reads cell p from node_GRIDS_ORDER[p].
        out = [None] * 25
        for p, k in enumerate(GRIDS_ORDER):
            out[k] = ours[p]
        assert all(o is not None for o in out)

    print(f"// {bank} bank, derived with daisy_grids/tools/groove_nodes/.")
    print(f"// Drop-in replacement for the node_0..node_24 block in grids/resources.cc.")
    print(f"// Node order is permuted to match Grids' own drum_map[5][5]; do not reorder.")
    print(f"// SPDX-License-Identifier: GPL-3.0-or-later")
    for n in range(25):
        print(f"const prog_uint8_t node_{n}[] PROGMEM = {{")
        for r in range(12):
            row = out[n][r * 8:(r + 1) * 8]
            print(' ' + ''.join(f'{v:7d},' for v in row))
        print("};")


if __name__ == '__main__':
    main()
