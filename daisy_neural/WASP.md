# Capturing the Wasp

One recording, two kinds of network: **NAM A2 snapshots** at fixed cutoffs, and a **GRU
with the cutoff as a knob**. The real Doepfer A-124 is the reference both are judged
against. The plan and its reasoning are in [CLAUDE.md](CLAUDE.md), "The Wasp". This page
is the bench sheet. Everything runs through [`tools/wasp_capture.py`](tools/wasp_capture.py),
from `daisy_neural/`.

## What one session records

A session is one knob setting: Lev, Frq, Res and Mix set by ear and then left alone. It
takes about 23 minutes, hands off.

| Takes | What plays | What it is for |
|---|---|---|
| `snap1`–`snap5`, 3:13 each | NAM's `input.wav`, bit for bit, with CV 1 held at one voltage per take | One A2 capture per cutoff. Also the GRU's fixed-setting comparison, trained on the same take |
| `sweep1`, `sweep2`, 3:03 each | Oscillator notes and glides at 0.5–9 V peak, noise, sine sweeps, riffs, rests. CV 1 holds, ramps and wanders | The GRU's knob |
| `test`, about 1:11 | A riff the networks never hear, with CV between the snapshots, on them, then a slow ramp up and back | The A/B, and the leftovers |

Both outputs are recorded at once, LP/HP mix and BP, so every take gives two datasets.

## Patching

| ES-10 | | Wasp |
|---|---|---|
| OUT 1 | → | AUDIO IN |
| OUT 2 | → | CV 1 (no attenuator, roughly 1 V/oct) |
| OUT 3 | → | ES-10 IN 3, a cable straight back (the loopback) |
| IN 1 | ← | LP/HP OUT |
| IN 2 | ← | BP OUT |

CV 2 stays unpatched, its knob at 0. The loopback measures the latency at the start and
the end of every take, which is how a dropped buffer gets caught.

## Once, before the first session

1. **ES-10 jumper to DC.** It sits at the top of the board, labelled HPF (manual p. 6).
   Leave inputs 1–3 at modular level, with no jumpers on the input header.
2. **Focusrite at 48 kHz.** In Focusrite Control, route three playback channels to ADAT
   1–3 and ADAT inputs 1–3 to three record channels.
3. **`captures/wasp/rig.json`.** It appears with the first `make rigcheck`. Fill in
   `device` (part of the name `devices` prints, for example `Scarlett`) and the channel
   numbers from step 2.
4. **NAM's input file** goes in `captures/wasp/input.wav`. It's already there on the
   MacBook Air. On another Mac, download it from NAM's [GUI trainer
   page](https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/gui.html). The
   tool refuses any file that isn't v3.0.0 (MD5 `36cd1af62985c2fac3e654333e36431e`),
   because the trainer wouldn't recognise it.
5. **sounddevice**, if this Mac's `.venv` predates it:
   `.venv/bin/pip install -r requirements.txt`. The first `record` makes macOS ask
   whether the terminal may use the microphone. Allow it.

```
.venv/bin/python tools/wasp_capture.py devices
```

## 1. Rig check (5 minutes)

Patch everything above except OUT 2: leave that on a loose cable with the multimeter on
it, DC volts, tip to sleeve.

```
.venv/bin/python tools/wasp_capture.py make rigcheck
.venv/bin/python tools/wasp_capture.py record captures/wasp/rigcheck.wav
```

Read the meter **between 4 and 19 seconds**, while OUT 2 holds half of full scale. Then:

```
.venv/bin/python tools/wasp_capture.py check captures/wasp/rigcheck.rec.wav --meter-volts 5.03 --save
```

Good looks like this:
- a latency of a few milliseconds, with the burst found far above the floor;
- **"DC gets through"**;
- a loopback gain near 1;
- both Wasp outputs well above −60 dBFS during the saw.

`--save` writes the ES-10's volts per full scale into `rig.json`, so every later file is
made in volts.

## 2. The listening tour

Power the Wasp **half an hour** before recording. It's CMOS, and NAM's trainer checks
that the start and end of each take agree, so drift fails a take.

Patch OUT 2 to CV 1, then:

```
.venv/bin/python tools/wasp_capture.py make ladder --from -4 --to 6
.venv/bin/python tools/wasp_capture.py record captures/wasp/ladder.wav
```

That plays the Mirth test riff once per volt and prints the cue times. Play with Lev,
Res, Mix and Frq around it, and choose:

- **the setting** for the session: Lev, Frq, Res and Mix. Lev is the drive. `input.wav`
  has to play unscaled, so the knob does the work. Doepfer: the Wasp can't self-oscillate,
  so high resonance is safe ground for the networks.
- **the sweep range**: the span of CV 1 where the cutoff does something you'd use.
- **five snapshot voltages** inside that range: where you'd actually leave the knob.

Write down what you like and where it turns nasty. The session makes a `notes.md` for
that, and it's half a script for the video.

## 3. Record the session (about 23 minutes)

```
.venv/bin/python tools/wasp_capture.py make session lp-res7 --snapshots=-2,0,1.5,3,4.5 \
    --cv-range=-3,5 --knobs "Lev 7, Frq noon, Res 7, Mix LP"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res7
```

Use the `=` with negative lists. `record` runs every take in turn, prints the peaks after
each, and skips any already recorded. Don't touch the rack while it runs. To redo one
take: `--only sweep2 --overwrite`.

## 4. Split and check

```
.venv/bin/python tools/wasp_capture.py split captures/wasp/lp-res7
```

Each take should read **OK**. The flags:

| Flag | Meaning | Do |
|---|---|---|
| `DROPOUT` | The latency changed during the take, so samples were lost | Record that take again |
| `start and end differ` | NAM's own check: the validation passage came out differently at 0:00 and 3:01 | The Wasp drifted. Warm it up longer, then record again |
| `full scale reached` | A Wasp output clipped the ES-10 input (±12 V) | Turn Lev down |
| `loopback only … clean` | The loopback cable isn't right | Check OUT 3 → IN 3 |

What comes out, in `captures/wasp/<session>/`:
- **`nam/`**: one 24-bit file per snapshot and output, exactly the length of `input.wav`
  and aligned to it. Train them in NAM's GUI (`nam`), with `input.wav` as the input. Its
  simplified trainer always builds A2.
- **`gru/`**: one `.npz` per take, holding `x` (the audio as played), `cv_v` (volts at
  CV 1) and `y_lphp` and `y_bp` (the Wasp). This is the GRU's data. Its training script
  is the next piece of work.
- **`listen/`**: the real Wasp on the sweeps and the held-out take, plus the dry held-out
  riff.
- **`report.txt`** and **`split.json`**: the numbers behind the flags.

## Then: hearing the difference

- **A fixed setting:** A2 against the GRU, both trained on the same snapshot take.
- **The knob:** the held-out take. Between the snapshots A2 can only jump; the GRU
  should glide.
- **The leftovers:** subtract each network's output from the real Wasp's and listen to
  what's left. That's what each network failed to learn.
