"""Render and check one of Mirth's FX bank effects, old beside new (FX.md, step 3).

    .venv/bin/python tools/fx_listen.py classic --new 0.5 0.6 0.2 --old 0.85 0.5 0
    .venv/bin/python tools/fx_listen.py classic --sweep      # a few settings, new only

Sources, all 48 kHz stereo: the dry synth riff (quant_study/in_dry.wav, 4 s, then 6 s of
tail), 15 s of David's guitar (Movies/Mirth/guitar, 2 Nov, resampled from 44.1 kHz) and a
single click (for the reverbs' decay). For each it writes, under fx_listen/<effect>/:
  <source>_new_wet.wav, <source>_new_mix.wav (MIX at noon, equal power, as the module)
  <source>_old_wet.wav, <source>_old_mix.wav (the old MultiFX, at the --old settings)
and prints checks: NaNs, clicks, the wet level against the dry, stereo correlation, and
for the click the RT60 measured from its decay. The knobs' meanings differ between old and
new (the new ones are in fx_bank.h's kDefs), so --old sets the old ones to match by ear.
"""
import argparse, os, subprocess, sys, glob
import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FS = 48000
EFFECTS = ["classic", "plate", "tank", "shimmer", "ping", "tape", "multitap", "echoverb"]


def sources():
    riff, sr = sf.read(os.path.join(ROOT, "quant_study/in_dry.wav"), dtype="float32")
    assert sr == FS
    riff = np.concatenate([riff, np.zeros(6 * FS, np.float32)])
    riff = np.stack([riff, riff], 1)
    g = sorted(glob.glob(os.path.expanduser("~/Documents/Movies/Mirth/guitar/*_ch01-02.wav")))[0]
    gx, gsr = sf.read(g, dtype="float32", start=0, frames=int(15 * 44100))
    gx = signal.resample_poly(gx, 160, 147, axis=0).astype(np.float32)  # 44.1 -> 48 kHz
    gx = np.concatenate([gx, np.zeros((5 * FS, 2), np.float32)])
    gx *= 0.5 / max(np.abs(gx).max(), 1e-9)
    click = np.zeros((10 * FS, 2), np.float32)
    click[FS // 10] = 0.5
    return {"riff": riff, "guitar": gx, "click": click}


def render(exe, effect, p, x):
    r = subprocess.run([os.path.join(HERE, exe), str(effect)] + [f"{v:.4f}" for v in p],
                       input=x.astype(np.float32).tobytes(), capture_output=True, check=True)
    return np.frombuffer(r.stdout, np.float32).reshape(-1, 2)


def mix(dry, wet, m=0.5):
    return np.sin(m * np.pi / 2) * wet + np.cos(m * np.pi / 2) * dry


def rms_db(x):
    return 10 * np.log10(np.mean(x.astype(np.float64) ** 2) + 1e-20)


def clicks(x):
    """Samples whose second difference stands far above the signal's own: a click detector.
    Counts spikes over 12x the 99.9th percentile of |d2|, which smooth audio never makes."""
    d2 = np.abs(np.diff(x, 2, axis=0)).max(1)
    ref = np.percentile(d2, 99.9) + 1e-9
    return int((d2 > 12 * ref).sum())


def rt60(x):
    """Schroeder backward integration of the click's response, the -5 to -35 dB slope."""
    e = np.sum(x.astype(np.float64) ** 2, 1)[::-1].cumsum()[::-1]
    e = 10 * np.log10(e / e.max() + 1e-30)
    t = np.arange(len(e)) / FS
    i5, i35 = np.argmax(e < -5), np.argmax(e < -35)
    if i35 <= i5:
        return float("nan")
    slope = np.polyfit(t[i5:i35], e[i5:i35], 1)[0]
    return -60 / slope


def check(name, dry, wet):
    nan = int(np.isnan(wet).sum())
    corr = np.corrcoef(wet[:, 0], wet[:, 1])[0, 1] if wet.std() > 0 else float("nan")
    line = (f"  {name:12s} NaN {nan}  clicks {clicks(wet):3d}  peak {20*np.log10(np.abs(wet).max()+1e-12):6.1f} dBFS"
            f"  wet-dry {rms_db(wet)-rms_db(dry):+5.1f} dB  L/R corr {corr:+.2f}")
    return line


def run(effect, newp, oldp, out):
    os.makedirs(out, exist_ok=True)
    idx = EFFECTS.index(effect)
    for src, x in sources().items():
        new = render("fx_render", idx, newp, x)
        sf.write(f"{out}/{src}_new_wet.wav", new, FS)
        sf.write(f"{out}/{src}_new_mix.wav", mix(x, new), FS)
        print(check(f"{src} new", x, new) + (f"  RT60 {rt60(new):.2f} s" if src == "click" else ""))
        if oldp is not None:
            old = render("fx_render_old", idx, oldp, x)
            sf.write(f"{out}/{src}_old_wet.wav", old, FS)
            sf.write(f"{out}/{src}_old_mix.wav", mix(x, old), FS)
            print(check(f"{src} old", x, old) + (f"  RT60 {rt60(old):.2f} s" if src == "click" else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("effect", choices=EFFECTS)
    ap.add_argument("--new", nargs=3, type=float, default=[0.5, 0.5, 0.3])
    ap.add_argument("--old", nargs=3, type=float)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    out = a.out or os.path.join(ROOT, "fx_listen", a.effect)
    run(a.effect, a.new, a.old, out)
    print("->", out)


if __name__ == "__main__":
    main()
