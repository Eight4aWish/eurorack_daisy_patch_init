"""
swords_glide.py — whether Swords' self-oscillation locks onto an outside tone or only bends
towards it, and how that depends on their relative levels (SWORDS.md, "Above the
threshold"; David's guess: it shifts rather than snaps, depending on the relative powers).
At each cutoff, Res CV held past the onset, a sine glides two octaves around the
oscillation, up then down, at each level. Run through swords_capture.py:

    .venv/bin/python tools/swords_capture.py make glide          # the takes (sw-glide)
    .venv/bin/python tools/swords_capture.py record captures/swords/sw-glide
    .venv/bin/python tools/swords_capture.py glide captures/swords/sw-glide
"""

import datetime
import json
import pathlib

import numpy as np
import soundfile as sf

import swords_capture as S

SR = S.SR
W = S.W
# Swords' self-oscillation (step 0): 168.9 Hz at V/OCT +4 V, rising 0.972 octave per volt;
# steady at every cutoff measured by Res CV 5.5 V.
OSC_HZ_AT_4V, OSC_OCT_PER_V = 168.9, 0.972
FREE, TAIL = 3.0, 2.0  # seconds of the oscillation alone, before and after the glides


def osc_hz(v):
    return OSC_HZ_AT_4V * 2 ** (OSC_OCT_PER_V * (v - 4.0))


def glide_audio(f0, peak_fs, glide_s):
    """Silence, then a sine from f0/2 up to 2 f0 and back down (log, glide_s each way),
    then silence. The phase is integrated, so the pitch is continuous; 20 ms fades. Returns
    the audio and the sine's pitch through the glides."""
    n = int(glide_s * SR)
    up = f0 / 2 * 4 ** (np.arange(n) / n)
    f = np.concatenate([up, up[::-1]])
    ph = 2 * np.pi * np.cumsum(f) / SR
    i = np.arange(2 * n)
    fade = np.minimum(1, np.minimum(i, 2 * n - 1 - i) / (0.02 * SR))
    return np.concatenate([np.zeros(int(FREE * SR)), peak_fs * fade * np.sin(ph), np.zeros(int(TAIL * SR))]), f


def make(args):
    rig = S.load_rig()
    vfs = float(rig["out_vfs"])
    cuts = [float(v) for v in args.cuts.split(",")]
    levels = [float(v) for v in args.levels.split(",")]
    d = S.ROOT / args.name
    if (d / "session.json").exists() and not args.overwrite:
        raise SystemExit(f"{S.rel(d)} already has a session; --overwrite to replace it")
    (d / "play").mkdir(parents=True, exist_ok=True)
    takes = []
    for v in cuts:
        for lv in levels:
            audio, _ = glide_audio(osc_hz(v), float(S.fs_of(lv, vfs, "glide level")), args.glide_s)
            name = f"glide_c{v:+.1f}_l{lv:.1f}"
            sf.write(d / "play" / f"{name}.wav", S.take4(audio, v / vfs, args.res / vfs), SR, subtype="FLOAT")
            takes.append({"name": name, "role": "glide", "cut_v": v, "res_v": args.res, "level_v": lv,
                          "osc_hz": osc_hz(v), "glide_s": args.glide_s, "payload_at": W.PAYLOAD_AT,
                          "payload_len": len(audio), "burst_at": W.BURST_AT, "rest_window": W.REST_WINDOW,
                          "seconds": round((W.PAYLOAD_AT + len(audio) + W.POST) / SR, 1)})
            print(f"  {name:20s} {takes[-1]['seconds']:6.1f} s  around {osc_hz(v):.0f} Hz")
    session = {"module": "swords", "filter": 1, "name": args.name, "kind": "glide",
               "made": datetime.datetime.now().isoformat(timespec="seconds"), "sr": SR, "out_vfs": vfs,
               "in_vfs": rig.get("in_vfs"), "res_v": args.res, "panel": args.panel, "takes": takes,
               "channels": {"play": ["audio -> Swords In A (13)", "cutoff CV -> V/OCT A (14)",
                                     "loopback -> ES-10 IN 3", "Res CV -> Resonance CV In A (9)"],
                            "rec": ["Swords Filter 1 Out (33)", "Swords Main Out (34)", "loopback"]}}
    (d / "session.json").write_text(json.dumps(session, indent=2) + "\n")
    print(f"{len(takes)} takes, {sum(t['seconds'] for t in takes) / 60:.1f} min to record")


def peak_hz(seg, lo, hi):
    """The strongest peak in [lo, hi] Hz, interpolated, and its level in dBV."""
    w = np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg * w, 4 * len(seg)))
    f = np.fft.rfftfreq(4 * len(seg), 1 / SR)
    m = np.where((f > lo) & (f < hi))[0]
    k = m[np.argmax(X[m])]
    a, b, c = np.log(X[k - 1:k + 2] + 1e-30)
    d = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + d) * SR / (4 * len(seg)), 20 * np.log10(X[k] * 2 / w.sum() / np.sqrt(2) + 1e-12)


def read(args):
    """Per take: the oscillation alone (its free pitch and level, dBV rms), then through each
    glide, in 170 ms frames, the output's strongest pitch near the oscillation against the
    input's. Locked: the output's pitch on the input's (within 5 cents) while the input is
    more than 15 cents from the free pitch. Pulled: in frames not locked, where the output's
    pitch is more than 30 cents from the input's, how far it has moved from free."""
    d = pathlib.Path(args.path)
    s = json.loads((d / "session.json").read_text())
    rig = S.load_rig()
    print("take                  free Hz  osc dBV  sine dBV |  up: locked, cents from free   down: locked"
          "        most pull outside the lock, cents")
    for t in s["takes"]:
        if not (d / "rec" / f"{t['name']}.wav").exists():
            continue  # not recorded yet
        play, _ = sf.read(d / "play" / f"{t['name']}.wav", dtype="float64", always_2d=True)
        rec, _ = sf.read(d / "rec" / f"{t['name']}.wav", dtype="float64", always_2d=True)
        lag, _ = W.latency(play[:, S.LOOP_OUT], rec[:, S.LOOP], t["burst_at"], W.BURST)
        y = rec[lag + t["payload_at"]:lag + t["payload_at"] + t["payload_len"], S.F1] * rig["in_vfs"]
        f0, n = t["osc_hz"], int(t["glide_s"] * SR)
        f_free, l_osc = peak_hz(y[int(0.5 * SR):int((FREE - 0.2) * SR)], f0 / 1.5, f0 * 1.5)
        _, f_in = glide_audio(f0, 1.0, t["glide_s"])
        N, H, g0 = 8192, 1024, int(FREE * SR)
        rows = {"up": [], "down": []}
        for i in range(0, 2 * n - N, H):
            fi = f_in[i + N // 2]
            fo, _ = peak_hz(y[g0 + i:g0 + i + N], f_free / 2.2, f_free * 2.2)
            c_in, c_off = 1200 * np.log2(fi / f_free), 1200 * np.log2(fo / fi)
            locked = abs(c_off) < 5 and abs(c_in) > 15
            pull = 1200 * np.log2(fo / f_free) if abs(c_off) > 30 else 0.0
            rows["up" if i + N // 2 < n else "down"].append((c_in, locked, pull))

        def span(rs):
            lk = [c for c, l_, _ in rs if l_]
            return f"{min(lk):+6.0f} to {max(lk):+6.0f}" if lk else "      none      "
        pulls = [p for rs in rows.values() for _, l_, p in rs if not l_]
        pull = max(pulls, key=abs) if pulls else 0.0
        l_in = 20 * np.log10(t["level_v"] / np.sqrt(2))
        print(f"{t['name']:20s} {f_free:7.1f}  {l_osc:6.1f}   {l_in:6.1f} |  {span(rows['up'])}"
              f"          {span(rows['down'])}       {pull:+6.0f}")
