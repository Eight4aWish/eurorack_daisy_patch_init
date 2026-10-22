#!/usr/bin/env python3
"""
mirth_input_fs.py — the patch.init's input full scale, measured through Mirth itself, so
that DRIVE at noon can feed the FILTERS networks the level they were trained on (the ES-10's
units, 10.96 V to full scale).

Patch: ES-10 OUT 1 -> Mirth IN_L, Mirth OUT_L -> ES-10 IN 1 (the loopback OUT 3 -> IN 3 if
it is still in). Mirth's MIX fully down, so its output is its input, digitally untouched.
A 220 Hz sine steps from 0.25 to 10.5 V peak; at each step, the output's level at 220 Hz
against the input's, and its distortion. Where the output stops following (the gain sags,
the distortion jumps) the input has reached digital full scale: that input level is the
full scale in volts. The rig's channels are captures/swords/rig.json's.

    .venv/bin/python tools/mirth_input_fs.py
"""

import json
import pathlib

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[1] / "captures/swords"
SR, F0 = 48000, 220.0
STEPS = [0.25, 0.5, 1.0, 1.5, 2.0] + [round(2.25 + 0.25 * i, 2) for i in range(34)]  # to 10.5 V
HOLD, GAP, PRE = 0.5, 0.1, 1.0


def main():
    import sounddevice as sd
    rig = json.loads((ROOT / "rig.json").read_text())
    vfs_out, vfs_in = float(rig["out_vfs"]), float(rig["in_vfs"])
    seg = int((HOLD + GAP) * SR)
    n = int(PRE * SR) + seg * len(STEPS) + SR
    audio = np.zeros(n)
    t = np.arange(int(HOLD * SR)) / SR
    ramp = np.minimum(1, np.minimum(t, HOLD - t) / 0.01)  # 10 ms fades, no clicks
    for i, v in enumerate(STEPS):
        a = int(PRE * SR) + i * seg
        audio[a:a + len(t)] = v / vfs_out * ramp * np.sin(2 * np.pi * F0 * t)
    play = np.zeros((n, len(rig["play_channels"])), np.float32)
    play[:, 0] = audio  # OUT 1: the audio; OUT 2 and 4 (Swords' CVs) stay at 0 V
    play[:, 2] = audio  # OUT 3: the loopback, for the latency
    print(f"playing {n / SR:.0f} s: 220 Hz, {STEPS[0]} to {STEPS[-1]} V peak in {len(STEPS)} steps")
    r = sd.playrec(play, samplerate=SR, device=rig["device"], dtype="float32", latency="high",
                   output_mapping=rig["play_channels"], input_mapping=rig["rec_channels"])
    sd.wait()
    sf.write(ROOT / "mirth_input_fs.rec.wav", r, SR, subtype="FLOAT")
    out, loop = r[:, 0].astype(np.float64), r[:, 2].astype(np.float64)
    # The latency from the loopback if it is in, else the rig's own, steady all of 2026-10-22
    # (12,369-12,376 samples). An unpatched input still reads a steady offset, so the test
    # is its swing, not its level; and a tone this steady correlates at every period, so the
    # loopback is read on the first step's onset.
    looped = np.std(loop) > 0.01
    if looped:
        on = int(PRE * SR)
        env = np.abs(loop - np.mean(loop[:on // 2]))
        lag = int(np.argmax(env > 0.5 * env[on:on + 3 * SR].max())) - on
    else:
        lag = 12370
    print(f"latency {lag} samples ({'loopback' if looped else 'the rig usual, no loopback'})")

    def tone(x):
        w = np.hanning(len(x)); X = np.abs(np.fft.rfft(x * w)); f = np.fft.rfftfreq(len(x), 1 / SR)
        amp = lambda h: X[np.argmin(np.abs(f - h * F0))] * 2 / w.sum()  # noqa: E731
        a1 = amp(1)
        thd = np.sqrt(sum(amp(h) ** 2 for h in range(2, 8))) / (a1 + 1e-12)
        return a1, 20 * np.log10(thd + 1e-12), np.abs(x).max()

    print(" in V peak   out V (220 Hz)   gain dB   distortion dB   out peak V")
    rows = []
    for i, v in enumerate(STEPS):
        a = int(PRE * SR) + i * seg + lag + int(0.1 * SR)
        a1, thd, pk = tone(out[a:a + int(0.3 * SR)])
        rows.append((v, a1 * vfs_in, 20 * np.log10(a1 * vfs_in / v + 1e-12), thd, pk * vfs_in))
        print(f"   {v:5.2f}       {a1 * vfs_in:6.3f}         {rows[-1][2]:+6.2f}       {thd:6.1f}        {pk * vfs_in:6.3f}")
    g0 = np.median([r_[2] for r_ in rows[:6]])
    knee = next((r_ for r_ in rows if r_[2] < g0 - 0.5 or r_[3] > -30), None)
    print(f"\nlow-level gain {g0:+.2f} dB (Mirth's output volts per input volt, dry)")
    print(f"output ceiling {max(r_[4] for r_ in rows):.2f} V peak: the input's full scale is that over the gain,"
          f" {max(r_[4] for r_ in rows) / 10 ** (g0 / 20):.2f} V peak")
    print("full scale: " + (f"the output stops following between {rows[rows.index(knee) - 1][0]} and {knee[0]} V peak"
                            if knee else f"not reached by {STEPS[-1]} V peak"))


if __name__ == "__main__":
    main()
