#!/usr/bin/env python3
"""Render Emilie's factory bank onto Sorrow's 16th-note grid.

Her 32 bytes per lane are ONE bar at Grids' 32nd-note step rate. Sorrow steps
once per 16th, so read raw they become TWO bars - a uniform 2x time stretch. Her
node 17 has a kick on all four quarters of a bar; raw, it comes out as a kick on
beats one and three of two bars, at half the density and half the speed.

Nothing was lost in that reading - almost all her values are on even slots - but
nothing being lost is not the same as the tempo being right.

So: merge each 32nd PAIR into one 16th, taking the louder of the two, giving her
bar in 16 steps; then repeat it for Sorrow's second bar. Chaos re-rolls per bar
now, so the two bars are not identical in play.

Merging rather than dropping the odd slots matters for exactly one node. Node 24
has every hit a 32nd late - swing written into the grid - and dropping would take
7 of its 15 hits. Merging lands them on the 16ths they were swung from, which is
the right rendering here because Sorrow takes its swing from the clock instead.
Across the bank: merge keeps 409 of her 415 hits, dropping keeps 401. The 6 lost
are pairs where both 32nds fire, which a 16th grid cannot represent.

    python3 fold_original.py > ../../src/grids_nodes.cpp
"""
import pathlib, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent

def fold(orig96: np.ndarray) -> np.ndarray:
    out = np.zeros((25, 96), dtype=np.uint8)
    for lane in range(3):
        sl = orig96[:, lane * 32:(lane + 1) * 32]
        bar = np.maximum(sl[:, 0::2], sl[:, 1::2])       # 16 sixteenths, louder of each pair
        out[:, lane * 32:(lane + 1) * 32] = np.concatenate([bar, bar], axis=1)
    return out

if __name__ == '__main__':
    A = fold(np.load(HERE / 'orig.npy'))
    np.save(HERE / 'original_folded.npy', A)
    print(f"wrote original_folded.npy", file=sys.stderr)
