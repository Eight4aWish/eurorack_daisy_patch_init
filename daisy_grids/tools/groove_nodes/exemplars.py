#!/usr/bin/env python3
"""Derive a 25-node drum map from real patterns, not cluster centroids.

Why this replaces som.py
------------------------
The SOM banks measured well on the coherence score and played flat. Reported by
ear first - "it does not sound like much is happening" - and the reason is that
the score was the wrong thing to optimise and the SOM was the wrong tool.

A self-organising map's objective function IS to minimise the difference between
neighbouring cells. Grids' X/Y interpolates between neighbours, so an algorithm
that makes neighbours similar is an algorithm that makes the knob boring. On top
of that its nodes are centroids - averages of thousands of patterns - and
averages regress toward each other. Twenty-five averages are less distinctive
than twenty-five archetypes, which is what Emilie picked by hand.

Measured as steps that change state when X or Y moves one cell, at mid density:

    Latin, SOM centroids        12.1 per edge,  46% of the map's range,  3 dead edges
    Grids' own                  13.8 per edge,  78% of the map's range,  0 dead edges
    real patterns, unarranged   37.9 per edge, 106% of the map's range,  0 dead edges
    real patterns, arranged     29.2 per edge,  78% of the map's range,  0 dead edges

A dead edge is a neighbour pair differing by three steps or fewer: a knob move
you cannot hear.

So: two stages, which is what Emilie did by hand.

1. CHOOSE by farthest-point sampling - twenty-five real patterns, each as far as
   possible from the ones already chosen. Archetypes, not averages.
2. ARRANGE on the 5x5 to minimise total edge length, so the map is a journey
   rather than a shuffle. Unarranged scores 106%, which means neighbours differ
   MORE than distant cells and every move is a jump.

Arranging lands on 78%, which is Emilie's number reached independently. That is
worth knowing: 78% looks like where "distinct patterns, well arranged" settles.

Usage
-----
    python3 exemplars.py <corpus.zip> <out-prefix> [--filter latin] [--bars 2]

    --bars 2   32 sixteenths, for Sorrow, which steps once per 16th
    --bars 1   one bar at Grids' own 32nd grid, written to the even slots
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import extract as ex  # noqa: E402

LANES = 3
FILTERS = {
    "latin": r"^(jazz|latin|afro|neworleans|reggae|highlife|middleeastern)",
    "all": r".",
}

# The 40 adjacent pairs on a 5x5.
EDGES = [(r * 5 + c, r * 5 + c + 1) for r in range(5) for c in range(4)] + \
        [(r * 5 + c, (r + 1) * 5 + c) for r in range(4) for c in range(5)]


def windows(path: str, style: str, steps: int) -> np.ndarray:
    """Every `steps`-long window in the corpus with something in it."""
    pat = re.compile(FILTERS[style])
    out = []
    for label, data in ex.sources(path):
        if not pat.match(label):
            continue
        try:
            evs, div = ex.note_ons(data, False)
        except Exception:
            continue
        if not evs or div <= 0:
            continue
        per16 = div / 4.0
        grid = collections.defaultdict(lambda: np.zeros((LANES, steps), dtype=np.float32))
        for tick, note, vel in evs:
            lane = next((l for l, s in enumerate(ex.LANES) if note in s), None)
            if lane is None:
                continue
            bar, off = divmod(int(round(tick / per16)), steps)
            g = grid[bar]
            g[lane, off] = max(g[lane, off], vel / 127.0)
        for k in sorted(grid):
            if (grid[k] > 0).sum() >= max(4, steps // 4):
                out.append(grid[k].reshape(-1))
    return np.array(out, dtype=np.float32)


def choose(X: np.ndarray, n: int = 25, seed: int = 7) -> np.ndarray:
    """Farthest-point sampling: each pick is the one least like everything picked.

    This is the opposite of clustering. k-means would hand back the centre of
    each crowd; this hands back the edges of the space, which is where the
    distinctive patterns live.

    Distance is measured on WHICH STEPS FIRE, not on raw velocity, and that
    matters more than it looks. On a quantised corpus like Lakh the same drum
    part appears thousands of times at different velocities: far apart as vectors
    of numbers, identical once they go through a threshold. Selecting on raw
    velocity filled six of Club's forty edges with pairs that measured different
    and sounded the same. Selecting on firing structure - with velocity kept as a
    light tie-break, so a busy pattern still beats a sparse one - does not.
    """
    fires = (X > 0.45).astype(np.float32)          # what you hear at mid density
    F = fires + 0.15 * X                            # velocity only breaks ties
    rng = np.random.default_rng(seed)
    idx = [int(rng.integers(len(X)))]
    d = np.abs(F - F[idx[0]]).mean(axis=1)
    for _ in range(n - 1):
        j = int(np.argmax(d))
        idx.append(j)
        d = np.minimum(d, np.abs(F - F[j]).mean(axis=1))
    return X[idx]


def arrange(E: np.ndarray, restarts: int = 12, iters: int = 40000,
            floor_pct: float = 0.0, floor_w: float = 2.0) -> np.ndarray:
    """Lay the 25 on the 5x5 so adjacent cells are as alike as they can be.

    Swap two cells, keep the swap if the total edge length went down. Crude, but
    the search space is 25! and the objective is cheap, so restarts beat
    cleverness. This is the only place similarity is wanted: the patterns are
    already as distinct as the corpus allows, and this decides the ROUTE between
    them.
    """
    D = np.array([[np.abs(E[i] - E[j]).mean() for j in range(25)] for i in range(25)])

    # Short edges make the map a journey rather than a shuffle. An edge can also
    # be too short - a neighbour pair so alike that the knob move is inaudible -
    # and `floor_pct` forbids that, quadratically. It defaults to off, because
    # measured on the Club bank it is not worth what it costs:
    #
    #     floor   per-edge   edge/map   dead at every density
    #     off       23.5       79%        1/40
    #     p10       25.8       91%        0/40
    #     p25       27.7      100%        0/40
    #
    # Buying that last dead edge costs the arrangement: at 100% neighbours differ
    # as much as distant cells, which is a shuffle, not a map. Off scores 79%,
    # which is Grids' own number, at the price of one inaudible move in forty.
    # Turn it on if a bank comes out with several.
    if floor_pct:
        FLOOR = np.percentile([D[i, j] for i in range(25) for j in range(i + 1, 25)], floor_pct)

        def cost(p):
            total = 0.0
            for a, b in EDGES:
                d = D[p[a], p[b]]
                total += d + (floor_w * (FLOOR - d) ** 2 if d < FLOOR else 0.0)
            return total
    else:
        cost = lambda p: sum(D[p[a], p[b]] for a, b in EDGES)  # noqa: E731
    best, best_c = None, float("inf")
    for seed in range(restarts):
        rng = np.random.default_rng(seed)
        p = list(rng.permutation(25))
        c = cost(p)
        for _ in range(iters):
            i, j = int(rng.integers(25)), int(rng.integers(25))
            if i == j:
                continue
            p[i], p[j] = p[j], p[i]
            c2 = cost(p)
            if c2 < c:
                c = c2
            else:
                p[i], p[j] = p[j], p[i]
        if c < best_c:
            best, best_c = list(p), c
    return E[best]


def shape(A: np.ndarray, orig96: np.ndarray, steps: int) -> np.ndarray:
    """Rank-match each lane onto Grids' own value distribution.

    Kept from som.py, and for the same reason: the density threshold and the
    fixed 192 accent line then behave exactly as they do with Grids' data, so
    nothing downstream has to change. It is monotonic per lane, so it cannot
    reorder a pattern - only rescale it.

    For a one-bar bank the 16 values are written to the EVEN slots of the 32-slot
    node, which is where Emilie writes hers, and matched against her even slots
    only: 94% of her odd slots are zero, so matching the lot drags everything
    toward silence.
    """
    out = np.zeros((25, 96), dtype=np.float32)
    for lane in range(LANES):
        sl = slice(lane * 32, (lane + 1) * 32)
        col = orig96[:, sl]
        target = np.sort((col if steps == 32 else col[:, 0::2]).reshape(-1))
        flat = A[:, lane * steps:(lane + 1) * steps].reshape(-1).copy()
        # Ties must break at random, not by index. A stable sort breaks them in
        # flat order, which is node-major, so every tied block gets handed out
        # low-to-high across the nodes - node 0 takes the quiet end of the block
        # and node 24 the loud end. On human playing that is harmless because
        # velocities are all different. On programmed MIDI it is fatal: Lakh has
        # 96% ties (28 distinct kick velocities across 800 slots), and the effect
        # was collapsing six adjacent pairs onto IDENTICAL firing patterns -
        # six knob moves that did nothing, out of forty.
        keys = np.lexsort((np.random.default_rng(1234 + lane).permutation(flat.size), flat))
        order = keys
        buf = np.empty_like(flat)
        buf[order] = target
        if steps == 32:
            out[:, sl] = buf.reshape(25, 32)
        else:
            out[:, lane * 32:(lane + 1) * 32:2] = buf.reshape(25, 16)
    return out


def report(A: np.ndarray, name: str) -> None:
    fire = lambda v: v > 128  # noqa: E731
    per_edge = np.mean([np.sum(fire(A[a]) != fire(A[b])) for a, b in EDGES])
    across = np.mean([np.sum(fire(A[i]) != fire(A[j])) for i in range(25) for j in range(25)
                      if abs(i // 5 - j // 5) + abs(i % 5 - j % 5) >= 3])
    # An edge that is dead at ONE density is usually just a sparse setting; an
    # edge dead at EVERY density is a knob move that never does anything. Only
    # the second is a defect, and reporting the first was making good banks look
    # broken - Latin shows three dead at density 64 and none anywhere else.
    dead = sum(1 for a, b in EDGES
               if all(np.sum((A[a] > t) != (A[b] > t)) <= 3
                      for t in (191, 159, 127, 95, 63)))
    print(f"  {name:<26} per-edge {per_edge:5.1f}   across {across:5.1f}   "
          f"edge/map {100 * per_edge / across:4.0f}%   dead {dead}/40")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("corpus")
    ap.add_argument("prefix")
    ap.add_argument("--filter", default="all", choices=sorted(FILTERS))
    ap.add_argument("--bars", type=int, default=2, choices=(1, 2))
    args = ap.parse_args()

    steps = 32 if args.bars == 2 else 16
    X = windows(args.corpus, args.filter, steps)
    print(f"{args.prefix}: {len(X)} windows of {steps} steps, filter={args.filter}")

    E = choose(X) * 255.0
    if steps == 32:
        report(E, "chosen, unarranged")
    E = arrange(E)
    if steps == 32:
        report(E, "chosen, arranged")

    orig96 = np.load(pathlib.Path(__file__).resolve().parent / "orig.npy")
    A = shape(E, orig96, steps)
    report(A, "after histogram match")
    np.save(f"{args.prefix}_nodes.npy", A)
    print(f"  wrote {args.prefix}_nodes.npy")


if __name__ == "__main__":
    main()
