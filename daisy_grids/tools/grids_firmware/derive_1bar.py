"""Derive a Grids bank at ONE bar, on the 16th-note slots.

Sorrow and real Grids step at different rates, and it matters:

  real Grids  kPulsesPerStep = 3 at 24 ppqn -> 8 steps/quarter, 32nd grid, ONE bar
  Sorrow      4 steps/quarter, 16th grid, so a 32-step pattern is TWO bars

Sorrow runs slower on purpose - swing is a 16th-note feel, so the steps have to
come off a 16th-note clock for something like Pam's to shuffle them.

So ../groove_nodes/ is right for Sorrow and wrong for Grids: it cuts 32-sixteenth
(two-bar) windows, which on Grids play as one bar of 32nds - double time. This
derives the same way but over ONE bar of 16 sixteenths, then writes those 16
values onto the EVEN slots of the 96-byte node and leaves the odd ones at zero.

Grids' native grid is 32nds, so that is what this quantises to - NOT 16ths. The
odd slots are not spare: Émilie uses 49 of them across 5 nodes, and they carry
the two things a 16th grid cannot express.

  node_23  hats in equal pairs at 5,7 / 11,13 / 21,23 / 29,31 - 32nd-note ROLLS
  node_24  kick, snare and hat at 5, 13, 21, 29 with nothing on 4, 12, 20, 28 -
           every hit a 32nd late, which is SWING written into the grid

Groove MIDI is human drummers, so the source has both. Quantising to 16ths threw
them away; at 32nds they survive.

    python3 derive_1bar.py groove.zip out_prefix [--filter latin]
"""
import sys, re, pathlib, collections
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'groove_nodes'))
import extract as ex                                   # reuse the MIDI reader

LANES, STEPS_1BAR = 3, 32   # one bar at 32nd-note resolution
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
        per32 = div / 8.0
        grids = collections.defaultdict(lambda: np.zeros((LANES, STEPS_1BAR), dtype=np.float32))
        for tick, note, vel in evs:
            lane = next((l for l, s in enumerate(ex.LANES) if note in s), None)
            if lane is None:
                continue
            step = int(round(tick / per32))
            bar, off = divmod(step, STEPS_1BAR)
            g = grids[bar]
            g[lane, off] = max(g[lane, off], vel / 127.0)
        for k in sorted(grids):
            g = grids[k]
            if (g > 0).sum() >= 8:                     # same floor as groove_nodes
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
    """Histogram-match per lane against Émilie's whole lane, all 32 slots.

    Rank-for-rank, so we inherit her exact density and dynamics - the same count
    of non-zeros and the same value distribution - while our own data decides
    WHERE they land. Steps our source never hit rank lowest and take her zeros."""
    out = np.zeros((25, 96), dtype=np.float32)
    for lane in range(LANES):
        target = np.sort(orig96[:, lane * 32:(lane + 1) * 32].reshape(-1))
        flat = w[:, lane * STEPS_1BAR:(lane + 1) * STEPS_1BAR].reshape(-1).copy()
        order = np.argsort(flat, kind='stable')
        buf = np.empty_like(flat); buf[order] = target
        out[:, lane * 32:(lane + 1) * 32] = buf.reshape(25, STEPS_1BAR)
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
