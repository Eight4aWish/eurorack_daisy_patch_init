"""Is the warble the network's own? WASP_LP_16's network (lp_cr_gru16_s3).
1. Its own ringing modes: the GRU linearised at rest (audio 0, cutoff V, Res R), the
   eigenvalues of dh'/dh -> frequency and decay time of each mode.
2. The same input as the real Wasp's Res 9 test take, network against Wasp: per hold, how
   much the gain at the resonance (output envelope over input envelope, a third of an
   octave either side of the network's mode) wobbles at 3-20 Hz, the warble, in dB rms.
   The gain, not the level, so the riff's own note pattern cancels.

    .venv-nam/bin/python tools/wasp_warble.py   (reads ~/wasp_lp: the run and lp-res9)
"""
import copy, pathlib, sys
import numpy as np
import torch
from scipy import signal

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import train_gru as T  # noqa: E402
import control_response as C  # noqa: E402

SR = 48000
run = pathlib.Path.home() / "wasp_lp/gru_runs/lp_cr_gru16_s3"
model, inputs, out, scale = C.load_model(run)
g = model.rnn
Wih, Whh = g.weight_ih_l0.detach().double(), g.weight_hh_l0.detach().double()
bih, bhh = g.bias_ih_l0.detach().double(), g.bias_hh_l0.detach().double()
H = g.hidden_size
print("inputs", inputs, "scale", T.SCALE)


def step(h, x):
    gi, gh = Wih @ x + bih, Whh @ h + bhh
    r = torch.sigmoid(gi[:H] + gh[:H]); z = torch.sigmoid(gi[H:2 * H] + gh[H:2 * H])
    n = torch.tanh(gi[2 * H:] + r * gh[2 * H:])
    return (1 - z) * n + z * h


def feats(cv, res):
    v = {"audio": 0.0, "cv": cv * T.SCALE["cv"], "res": res * T.SCALE["res"]}
    return torch.tensor([v[k] for k in inputs], dtype=torch.float64)


print("\n1. The network's own modes at rest (slowest-decaying complex pairs): frequency, decay to 1/e")
for res in (6.0, 9.0):
    for cv in (2.0, 4.0, 6.0, 8.0, 10.0):
        x = feats(cv, res)
        h = torch.zeros(H, dtype=torch.float64)
        for _ in range(48000):  # settle to the resting state for this conditioning
            h = step(h, x)
        J = torch.autograd.functional.jacobian(lambda hh: step(hh, x), h).numpy()
        ev = np.linalg.eigvals(J)
        cx = ev[np.imag(ev) > 1e-9]
        cx = cx[np.argsort(-np.abs(cx))][:3]
        desc = ", ".join(f"{np.angle(e) * SR / (2 * np.pi):6.0f} Hz {(-1 / (SR * np.log(abs(e)))) * 1000:6.1f} ms" for e in cx)
        print(f"  Res {res:.0f}  cutoff {cv:4.1f} V:  {desc}")

print("\n2. Res 9 test take, the same input: network v the real Wasp")
take = T.Take(pathlib.Path.home() / "wasp_lp/lp-res9/gru/test.npz")
t = copy.copy(take); t.res = 9.0
_, o = T.evaluate(model, [(t, 0, t.n, T.SETTLE, False)], inputs, out, "cpu", keep=True, out_scale=scale)
net = o[0].astype(np.float64)
real = take.target(0, take.n, out).astype(np.float64)

MODE = {2: 46, 4: 204, 6: 886, 8: 2607, 10: 5141}  # the network's main mode at rest, Res 9 (part 1)
x = take.features(0, take.n, ["audio"])[:, 0].astype(np.float64)

def gain_wobble(y, a, b, fr):
    sl = slice(int(a * SR), int(b * SR))
    sos = signal.butter(4, [fr * 2 ** (-1 / 3), min(fr * 2 ** (1 / 3), 20000)], "bandpass", fs=SR, output="sos")
    ey = np.abs(signal.hilbert(signal.sosfiltfilt(sos, y[sl])))
    ex = np.abs(signal.hilbert(signal.sosfiltfilt(sos, x[sl])))
    k = signal.butter(2, 40, fs=SR, output="sos")
    sy, sx = signal.sosfiltfilt(k, ey)[::48], signal.sosfiltfilt(k, ex)[::48]; fl = 1e-3 * sx.max(); gy = 20 * np.log10(np.maximum(sy, fl)) - 20 * np.log10(np.maximum(sx, fl)); gy = gy[sx > 10 * fl]
    gy = gy - gy.mean()
    f, p = signal.welch(gy, 1000, nperseg=1024)
    band = (f >= 3) & (f <= 20)
    return np.sqrt(p[band].sum() * (f[1] - f[0])), f[band][np.argmax(p[band])], 20 * np.log10(np.sqrt(np.mean(y[sl] ** 2)) + 1e-12)

print("  input in band: level, dB re its loudest band"); print("  hold  band (the resonance)   gain wobble at 3-20 Hz, dB rms: net / Wasp    main rate Hz: net / Wasp")
for v, (a, b) in C.HOLDS.items():
    wn, rn, _ = gain_wobble(net, a, b, MODE[v])
    ww, rw, _ = gain_wobble(real, a, b, MODE[v])
    print(f"  {v:2d} V   {MODE[v]:5d} Hz +-1/3 oct      {wn:5.2f} / {ww:5.2f}                          {rn:4.1f} / {rw:4.1f}")
