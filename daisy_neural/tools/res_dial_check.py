"""The Res dial as Mirth maps it (main.cpp kDial, kNetRes, kFb), against the real Wasp: the
resonant band's rise at each dial setting, and where the network makes a tone of its own.

    .venv-nam/bin/python tools/res_dial_check.py      (reads ~/wasp_lp: lp_cr_gru16_s3 and the lp-res* takes)
"""
import io, contextlib, pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import notamp_ideas as N
import control_response as C
with contextlib.redirect_stdout(io.StringIO()):
    from notamp_spectra import peak, wasp_ring
W = pathlib.Path.home() / "wasp_lp"; SR = 48000; END = int(24.5 * SR)
DIAL = [0, 7, 9, 10, 11]; NET = [0, 7, 12, 12, 12]; FB = [0, 0, 0, 0.15, 0.5]
def curve(d): return float(np.interp(d, DIAL, NET)), float(np.interp(d, DIAL, FB))
z = np.load(W / "lp-res6/gru/test.npz"); x, cv = z["x"][:END].astype(np.float64), z["cv_v"][:END].astype(np.float64)
def ring_band(v):
    f = 2 ** (3.3 + 0.935 * v)
    return next(i for i, (lo, hi) in enumerate(C.BANDS) if lo <= f < hi)
holds = [4, 6, 8, 10]
def res_change(levels, base):
    return np.mean([levels[v][ring_band(v)] - base[v][ring_band(v)] for v in holds])
base = C.band_levels(N.run_gru(x, cv, np.full(END, 6.0), 0.0))
wasp6 = C.band_levels(np.load(W / "lp-res6/gru/test.npz")["y_lphp"][:END].astype(np.float64))
print("dial   network Res  feedback   resonance band vs dial 6   real Wasp at that mark")
for d in (7, 8, 9, 9.5, 10, 10.5, 11):
    r, fb = curve(d)
    lv = C.band_levels(N.run_gru(x, cv, np.full(END, r), fb))
    wtxt = ""
    if float(d).is_integer() and d <= 10:
        wl = C.band_levels(np.load(W / f"lp-res{int(d)}/gru/test.npz")["y_lphp"][:END].astype(np.float64))
        wtxt = f"{res_change(wl, wasp6):+6.2f}"
    print(f"{d:5.1f}   {r:6.1f}     {fb:5.2f}          {res_change(lv, base):+6.2f}                  {wtxt}")
xp = N.phrase(); n = len(xp); cut = np.linspace(2, 10, n)
for d in (9.5, 10, 10.5, 11):
    r, fb = curve(d)
    y = N.run_gru(xp, cut, np.full(n, r), fb)
    notes = np.sqrt(np.mean(y[np.abs(xp) > 0.05] ** 2)); cells = []
    for k in range(16):
        a, b = int((k * 0.5 + 0.425) * SR), int((k * 0.5 + 0.495) * SR)
        lvl = 20 * np.log10(np.sqrt(np.mean(y[a:b] ** 2)) / notes + 1e-12); v = cut[(a + b) // 2]
        if lvl > -15:
            f0, _ = peak(y[a:b], lo=25); lo, hi = wasp_ring(v)
            cells.append(f"{v:.1f}V {f0:.0f}{'*' if lo * 0.9 <= f0 <= hi * 1.1 else ''}")
    print(f"dial {d}: self-made tone at " + (", ".join(cells) if cells else "none") + "   (* within 10% of where the Wasp rings)")
