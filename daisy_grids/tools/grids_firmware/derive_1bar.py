"""SUPERSEDED by ../groove_nodes/exemplars.py --bars 1, which is what ships.

This derives its 25 nodes with a self-organising map. That measured well and
played flat: a SOM's objective function IS to minimise the difference between
neighbouring cells, which is the opposite of what a morphing X/Y wants, and its
nodes are centroids - averages regress toward each other. Measured per cell move,
these tables scored 9.4-10.9 against the exemplar method's 16.4-18.1.

Kept for the even/odd slot analysis below, which still holds and is not written
down anywhere else. Do not ship from it.

Derive a Grids bank at ONE bar, on the 16th-note slots.

Sorrow and real Grids step at different rates, and it matters:

  real Grids  kPulsesPerStep = 3 at 24 ppqn -> 8 steps/quarter, 32nd grid, ONE bar
  Sorrow      4 steps/quarter, 16th grid, so a 32-step pattern is TWO bars

Sorrow runs slower on purpose - swing is a 16th-note feel, so the steps have to
come off a 16th-note clock for something like Pam's to shuffle them.

So ../groove_nodes/ is right for Sorrow and wrong for Grids: it cuts 32-sixteenth
(two-bar) windows, which on Grids play as one bar of 32nds - double time. This
derives the same way but over ONE bar of 16 sixteenths, then writes those 16
values onto the EVEN slots of the 96-byte node and leaves the odd ones at zero.

Quantised to 16ths and written onto the EVEN slots, leaving the odd ones at zero.

The odd slots are real - Émilie uses 49 of them across 5 nodes, and they carry
what a 16th grid cannot express:

  node_23  hats in equal pairs at 5,7 / 11,13 / 21,23 / 29,31 - 32nd-note ROLLS
  node_24  kick, snare and hat at 5, 13, 21, 29 and nothing on 4, 12, 20, 28 -
           every hit a 32nd late, which is SWING written into the grid

We deliberately do not use them. Groove MIDI is unquantised human drumming, so
ingesting at 32nds does not capture intent, it captures deviation: a drummer
20 ms late becomes a note genuinely displaced by a 32nd. Measured, that put 265
values on odd slots against Émilie's 49 - five times her rate, none of it meant.

A 16th grid with an irregular clock is the better trade. Swing belongs in the
clock, where a player controls it, not baked into the pattern where they cannot.
And a Grids owner who wants swing in the pattern still has Grids' own swing mode.

    python3 derive_1bar.py groove.zip out_prefix [--filter latin]
"""
import sys, re, pathlib, collections
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'groove_nodes'))
import extract as ex                                   # reuse the MIDI reader

LANES, STEPS_1BAR = 3, 16   # one bar of 16ths; written to the even slots
FILTERS = {
    'latin': r'^(jazz|latin|afro|neworleans|reggae|highlife|middleeastern)',
    'all':   r'.',
}


def one_bar_patterns(path, style_filter):
    """Same reader as groove_nodes, but 16-sixteenth windows instead of 32."""
    pats, styles = [], []
    pat = re.compile(FILTERS[style_filter])
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
        grids = collections.defaultdict(lambda: np.zeros((LANES, STEPS_1BAR), dtype=np.float32))
        for tick, note, vel in evs:
            lane = next((l for l, s in enumerate(ex.LANES) if note in s), None)
            if lane is None:
                continue
            step = int(round(tick / per16))
            bar, off = divmod(step, STEPS_1BAR)
            g = grids[bar]
            g[lane, off] = max(g[lane, off], vel / 127.0)
        for k in sorted(grids):
            g = grids[k]
            if (g > 0).sum() >= 4:                     # half the window, so half the floor
                pats.append(g.reshape(-1)); styles.append(label)
    return np.array(pats, dtype=np.float32), np.array(styles)


def train_som(X, seed=0xA1B2C3, epochs=30):
    rng = np.random.default_rng(seed)
    N = X.shape[0]
    W = H = 5
    coords = np.array([(r, c) for r in range(H) for c in range(W)], dtype=np.float32)
    w = X[rng.choice(N, W * H, replace=False)].copy()
    lr0, lr1, r0, r1 = 0.5, 0.01, 2.5, 0.45
    for ep in range(epochs):
        t = ep / (epochs - 1)
        lr, rad = lr0 * (lr1 / lr0) ** t, r0 * (r1 / r0) ** t
        for i in rng.permutation(N):
            x = X[i]
            bmu = np.argmin(((w - x) ** 2).sum(axis=1))
            d2 = ((coords - coords[bmu]) ** 2).sum(axis=1)
            w += lr * np.exp(-d2 / (2 * rad * rad))[:, None] * (x - w)
    return w


def shape_to_nodes(w, orig96):
    """Histogram-match per lane against Émilie's EVEN-slot values, then write to
    the even slots.

    Matched against her even slots only, not the whole lane: 94% of her odd slots
    are zero, so matching the lot would drag our distribution toward silence."""
    out = np.zeros((25, 96), dtype=np.float32)
    for lane in range(LANES):
        even = orig96[:, lane * 32:(lane + 1) * 32][:, 0::2].reshape(-1)
        target = np.sort(even)
        flat = w[:, lane * STEPS_1BAR:(lane + 1) * STEPS_1BAR].reshape(-1).copy()
        order = np.argsort(flat, kind='stable')
        buf = np.empty_like(flat); buf[order] = target
        out[:, lane * 32:(lane + 1) * 32:2] = buf.reshape(25, STEPS_1BAR)
    return out.astype(np.uint8)


def coherence(A):
    d = lambda a, b: np.abs(a.astype(float) - b.astype(float)).mean()
    g = A.reshape(5, 5, 96)
    nb = [d(g[r, c], g[r, c + 1]) for r in range(5) for c in range(4)] + \
         [d(g[r, c], g[r + 1, c]) for r in range(4) for c in range(5)]
    rand = [d(A[i], A[j]) for i in range(25) for j in range(25) if i != j]
    return 100 * (1 - np.mean(nb) / np.mean(rand))


def main():
    src, prefix = sys.argv[1], sys.argv[2]
    style = sys.argv[sys.argv.index('--filter') + 1] if '--filter' in sys.argv else 'all'
    orig96 = np.load(HERE / 'orig96.npy')

    X, S = one_bar_patterns(src, style)
    print(f"{prefix}: {len(X)} one-bar patterns, filter={style}")
    for g, n in collections.Counter(S).most_common(6):
        print(f"    {g:28} {n}")
    A = shape_to_nodes(train_som(X), orig96)
    np.save(f'{prefix}_1bar.npy', A)
    even = A.reshape(25, 3, 32)[:, :, 0::2]
    odd = A.reshape(25, 3, 32)[:, :, 1::2]
    print(f"  coherence {coherence(A):.1f}%   nonzero even {(even>0).sum()}  odd {(odd>0).sum()}")


if __name__ == '__main__':
    main()
