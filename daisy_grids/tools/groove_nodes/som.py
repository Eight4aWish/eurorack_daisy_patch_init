"""Train a 5x5 self-organising map over the corpus and shape it into Grids nodes.

A SOM is the right tool because Grids interpolates between *adjacent* map cells:
the arrangement has to be topology-preserving, not merely a clustering. Plain
k-means would give 25 sensible patterns in an arbitrary order, and sweeping X/Y
across them would jump rather than morph.
"""
import numpy as np

rng = np.random.default_rng(0xA1B2C3)
X = np.load('patterns.npy')                     # (N, 96), 0..1
N, D = X.shape

W, H = 5, 5
coords = np.array([(r, c) for r in range(H) for c in range(W)], dtype=np.float32)

# Start from real patterns rather than noise so units begin plausible.
w = X[rng.choice(N, W * H, replace=False)].copy()

EPOCHS = 30
lr0, lr1 = 0.5, 0.01
r0, r1 = 2.5, 0.45
for ep in range(EPOCHS):
    t = ep / (EPOCHS - 1)
    lr = lr0 * (lr1 / lr0) ** t
    rad = r0 * (r1 / r0) ** t
    for i in rng.permutation(N):
        x = X[i]
        bmu = np.argmin(((w - x) ** 2).sum(axis=1))
        d2 = ((coords - coords[bmu]) ** 2).sum(axis=1)
        h = np.exp(-d2 / (2 * rad * rad))[:, None]
        w += lr * h * (x - w)
    if ep % 10 == 0 or ep == EPOCHS - 1:
        q = np.mean([np.min(((w - X[i])**2).sum(axis=1)) for i in rng.choice(N, 500)])
        print(f"  epoch {ep:2d}  lr {lr:.3f}  radius {rad:.2f}  quantisation err {q:.4f}")

np.save('som_weights.npy', w)

# --- shape into Grids-like bytes -------------------------------------------
# Match the original tables' value distribution exactly by rank. The SOM decides
# which steps matter and how they rank; the histogram match makes the density
# threshold and the 192 accent line behave exactly as they do with Grids' own
# data, so the whole engine downstream is unchanged.
# Matched per lane, not globally. Globally, the snare lane's higher raw values
# captured most of the loud ranks and came out near-continuous and all strong -
# human ghost notes rendered as full hits. Per lane, each of kick/snare/hat
# inherits its own distribution from the original tables, so the ghosts land
# where they belong: as low levels that only fire when density is turned up.
orig = np.load('orig.npy')
shaped = np.empty((25, 96), dtype=np.float32)
for lane in range(3):
    sl = slice(lane * 32, (lane + 1) * 32)
    target = np.sort(orig[:, sl].reshape(-1))
    flat = w[:, sl].reshape(-1).copy()
    order = np.argsort(flat, kind='stable')
    out = np.empty_like(flat)
    out[order] = target
    shaped[:, sl] = out.reshape(25, 32)
np.save('som_nodes.npy', shaped)

# Also write the card format: raw bytes, 25 nodes of 96, row-major across the
# 5x5 map. This is the intended route for a personal bank - no rebuild, and
# nothing personal in a distributed binary.
#
# NOTE: SD loading is PARKED and the build defaults to SORROW_SD_USER_BANK=0, so
# a card file is not picked up today. See include/user_bank.h for how far the
# SDMMC debugging got. Until that lands, a derived bank has to be compiled in.
#
# Not .bin, deliberately: the Daisy bootloader flashes the first .bin it finds in
# the card root, so a bank file under that name could be written to QSPI as
# firmware. See daisy_grids/include/user_bank.h.
shaped.astype(np.uint8).tofile('sorrow_bank.dat')
print("wrote sorrow_bank.dat (%d bytes) - copy to the SD card root"
      % (25 * 96))

A = shaped
print(f"\nderived tables: zeros {(A==0).mean()*100:.1f}%  nonzero mean {A[A>0].mean():.0f}")
for i, lane in enumerate(("kick","snare","hat")):
    seg = A[:, i*32:(i+1)*32]; nz = seg > 0
    print(f"  {lane:5s}: {nz.mean()*100:5.1f}% active  nonzero mean {seg[nz].mean():5.0f}  "
          f">192 {(seg>192).mean()*100:4.1f}%")

def d(a, b): return np.abs(a - b).mean()
g = A.reshape(5, 5, 96)
nb = [d(g[r,c], g[r,c+1]) for r in range(5) for c in range(4)] + \
     [d(g[r,c], g[r+1,c]) for r in range(4) for c in range(5)]
rand = [d(A[i], A[j]) for i in range(25) for j in range(25) if i != j]
print(f"  neighbours {np.mean(nb):.1f} vs any pair {np.mean(rand):.1f}"
      f"  -> {100*(1-np.mean(nb)/np.mean(rand)):.0f}% more similar (Grids: 15%)")
