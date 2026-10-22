"""The Res dial's calibration (2026-10-22): for each Wasp Res mark, the network Res input
(FLT2's lp_cr_gru16_s3) whose change from Res 6 best matches the real Wasp's, over every
band and then over the band holding the resonance at each cutoff hold.

    .venv-nam/bin/python tools/res_dial_calibrate.py      (reads ~/wasp_lp: lp_cr_gru16_s3 and the lp-res* takes)
"""
import copy, pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import control_response as C
import train_gru as T
W = pathlib.Path.home() / "wasp_lp"
run = W / "gru_runs/lp_cr_gru16_s3"
model, inputs, out, scale = C.load_model(run)
ref = T.Take(W / "lp-res6/gru/test.npz")
END = int(24.5 * T.SR)
def net_levels(s):
    t = copy.copy(ref); t.res = float(s)
    _, o = T.evaluate(model, [(t, 0, END, T.SETTLE, False)], inputs, out, "cpu", keep=True, out_scale=scale)
    return C.band_levels(o[0])
wasp = {r: C.band_levels(T.Take(W / f"lp-res{r}/gru/test.npz").target(0, END, out)) for r in range(11)}
grid = np.arange(0, 20.01, 0.5)
net = {s: net_levels(s) for s in grid}
def change(levels, a, b):
    return np.concatenate([levels[a][v] - levels[b][v] for v in C.HOLDS])

BANDS = C.BANDS
def ring_band(v):
    f = 2 ** (3.3 + 0.935 * v)   # the middle of the Wasp's ring fit
    return next(i for i, (lo, hi) in enumerate(BANDS) if lo <= f < hi) if f < BANDS[-1][1] and f >= BANDS[0][0] else None
holds = [v for v in C.HOLDS if ring_band(v) is not None]
print("the band holding the resonance at each hold:", {v: BANDS[ring_band(v)] for v in holds})
print("\nresonance band, change from Res 6 (dB), averaged over the holds")
print("Wasp mark   Wasp     network at the same Res   network Res that matches it")
nets = {s_: np.mean([net[s_][v][ring_band(v)] - net[6.0][v][ring_band(v)] for v in holds]) for s_ in grid}
for r in range(11):
    w = np.mean([wasp[r][v][ring_band(v)] - wasp[6][v][ring_band(v)] for v in holds])
    best = min(grid, key=lambda s_: abs(nets[s_] - w))
    print(f"   {r:2d}      {w:+6.2f}        {nets.get(float(r), float('nan')):+6.2f}                  {best:5.1f}  ({nets[best]:+.2f})")
print("\nnetwork, resonance band, Res 10 to 20:", "  ".join(f"{s_:g}:{nets[s_]:+.2f}" for s_ in (10, 11, 12, 13, 14, 16, 18, 20)))
for v in holds:
    b = ring_band(v)
    print(f"  hold {v} V, band {BANDS[b]}: Wasp 6->10 {wasp[10][v][b]-wasp[6][v][b]:+.1f} dB, network 6->10 {net[10.0][v][b]-net[6.0][v][b]:+.1f}, 6->14 {net[14.0][v][b]-net[6.0][v][b]:+.1f}, 6->20 {net[20.0][v][b]-net[6.0][v][b]:+.1f}")
