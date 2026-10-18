# Capturing the Wasp

This is the bench sheet. The plan and its reasoning are in [CLAUDE.md](CLAUDE.md), "The
Wasp". Everything runs through [`tools/wasp_capture.py`](tools/wasp_capture.py), from
`daisy_neural/`.

The subject is the Wasp as **pure low-pass and pure high-pass**: the two end stops of
the Mix knob on its LP/HP output. The notch between them and the BP output are less
interesting. One output carries both, so each end stop is **its own series** of
sessions. The two series feed three kinds of network, with the real Doepfer A-124 as the
reference for all of them:

| Network | Learns | Trained on |
|---|---|---|
| **A2 (NAM's WaveNet)** | One capture per cutoff, at one resonance | The LP comparison session's snapshot takes |
| **GRU, cutoff** | The cutoff as a knob, at one resonance | The LP comparison session |
| **GRU, cutoff and resonance**, one for LP and one for HP | Both, as knobs | Every session of its series except the held-out one |

**Only the cutoff is under CV on the A-124.** Res, Lev and Mix are knobs. So the cutoff
is swept by the ES-10 inside each take, and the resonance is stepped between sessions:
**one session per Res position**. Lev and Frq stay where they are for both series; Mix
sits at its LP end stop for one series and its HP end stop for the other. The end stops
are exactly repeatable, which no mark on a dial is.

## The two series

| Series | Session | Res | Takes | Time |
|---|---|---|---|---|
| **LP** | **Comparison** (your favourite Res) | e.g. 6 | `snap1`–`snap5` (NAM's `input.wav` at five cutoff CVs, 3:13 each), `sweep1`–`sweep2`, `test`, `test_snaps` | about 24 min |
| | The others | e.g. 0, 2, 4, 8, 10 | `sweep1`–`sweep2`, `test` | about 7 min each |
| | Held out | e.g. 5 | `sweep1`–`sweep2`, `test`, never trained on | about 7 min |
| **HP** | Every Res position, held out included | the same seven | `sweep1`–`sweep2`, `test` | about 7 min each |

That's about 65 minutes of takes for LP and 50 for HP, plus resetting the knobs between
sessions. The A2 comparison is done once, on LP, the classic Wasp sound. It can be split
over two days if Lev and Frq aren't touched in between (photograph them), with the
half-hour warm-up each day.

| Take | What plays | What it is for |
|---|---|---|
| `snap1`–`snap5` | NAM's `input.wav`, bit for bit, with CV 1 held at one voltage per take | One A2 capture per cutoff, and the cutoff GRU's training data |
| `sweep1`, `sweep2` (3:03 each) | Oscillator notes and glides at 0.5–9 V peak, noise, sine sweeps, riffs, rests. CV 1 holds, ramps and wanders. New material at every Res position | The GRUs' knobs |
| `test` (1:00) | A riff no network hears, with CV at six holds across the range, then a slow ramp up and back. **Identical in every session**, so Res positions can be compared | The A/B, and the leftovers |
| `test_snaps` (0:39, comparison session only) | The same riff, with CV between the snapshots and on them | A2 jumping against the GRU gliding |

The BP output is recorded on IN 2 as well, because it costs nothing, but no network is
planned on it.

## Patching

| ES-10 | | Wasp |
|---|---|---|
| OUT 1 | → | AUDIO IN |
| OUT 2 | → | CV 1 (no attenuator, roughly 1 V/oct) |
| OUT 3 | → | ES-10 IN 3, a cable straight back (the loopback) |
| IN 1 | ← | LP/HP OUT |
| IN 2 | ← | BP OUT |

CV 2 stays unpatched; with nothing in it, its knob does nothing. The loopback measures the
latency at the start and the end of every take, which is how a dropped buffer gets caught.

## Once, before the first session

1. **ES-10 jumper to DC.** Block J6 at the top of the board has two columns, SLOW SD and
   HPF. The factory jumper lies across the top row, which is ground, so the inputs are
   AC-coupled. For DC it stands upright on the **HPF** column (manual p. 6, second photo).
   Leave inputs 1–3 at modular level, with no jumpers on the input header J5.
2. **Focusrite at 48 kHz.** On the Scarlett 16i16 4th Gen, ADAT 1–8 are **playback 7–14**
   out and **inputs 11–18** back. Inputs 7–8 are its internal loopback mix, not ADAT.
   Found 2026-10-18 by playing a quiet burst on each playback channel in turn and
   watching which input it came back on.
3. **`captures/wasp/rig.json`.** It appears with the first `make rigcheck`. Fill in
   `device` (part of the name `devices` prints, for example `Scarlett`) and the channel
   numbers from step 2.
4. **NAM's input file** goes in `captures/wasp/input.wav`. It's already there on the
   MacBook Air. On another Mac, download it from NAM's [GUI trainer
   page](https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/gui.html). The
   tool refuses any file that isn't v3.0.0 (MD5 `36cd1af62985c2fac3e654333e36431e`),
   because the trainer wouldn't recognise it. Only the comparison session needs it.
5. **sounddevice**, if this Mac's `.venv` predates it:
   `.venv/bin/pip install -r requirements.txt`. The first `record` makes macOS ask
   whether the terminal may use the microphone. Allow it.

```
.venv/bin/python tools/wasp_capture.py devices
```

## 1. Rig check (5 minutes)

**The meter first.** Unplug the loopback lead from IN 3, leaving it in OUT 3, and put the
multimeter (DC volts) across its tip and sleeve. Play the meter file and read it:

```
.venv/bin/python tools/wasp_capture.py make meter
.venv/bin/python tools/wasp_capture.py record captures/wasp/meter.wav
```

It holds half of full scale on OUT 2 and OUT 3 for 40 s. The "nothing came back on the
loopback" warning is expected, since the lead is on the meter. Plug it back into IN 3.

**Then the check**, with the meter's reading:

```
.venv/bin/python tools/wasp_capture.py make rigcheck
.venv/bin/python tools/wasp_capture.py record captures/wasp/rigcheck.wav
.venv/bin/python tools/wasp_capture.py check captures/wasp/rigcheck.rec.wav --meter-volts 5.48 --save
```

It plays a noise burst for the latency, DC holds on OUT 2 and OUT 3, then a 110 Hz saw
through the Wasp while CV 1 ramps the filter open, so a shut filter (Frq at zero) is not
mistaken for bad patching. `--save` writes what it measured into `rig.json`, so every
later file is made in volts.

What it read on 2026-10-18, so you know what good looks like:
- **latency 12,369 samples (258 ms)**: long, because the recorder asks for large buffers
  to avoid dropouts. It moves by a few samples each time the stream starts, which is why
  every take measures its own;
- **"DC gets through"**, with the loopback gain from the DC holds (0.916) and from the saw
  (0.9155) agreeing, and the saw's residual below −30 dB;
- **ES-10: 10.96 V per full scale out, 11.97 V in** (the meter read 5.48 V);
- **input offsets around +0.02 of full scale** (about 240 mV) on all three inputs, which
  the split subtracts;
- **both Wasp outputs 66 dB over their floor** once CV 1 opens the filter. The Wasp's
  floor at rest is about −77 dBFS, well above the ES-10's own −104.

## 2. The listening tour

Power the Wasp **half an hour** before recording. It's CMOS, and NAM's trainer checks
that the start and end of each take agree, so drift fails a take.

Patch OUT 2 to CV 1, then:

```
.venv/bin/python tools/wasp_capture.py make ladder --from -4 --to 6
.venv/bin/python tools/wasp_capture.py record captures/wasp/ladder.wav
```

That plays the Mirth test riff once per volt and prints the cue times. Run it again at a
few Res settings, and choose:

- **Lev and Frq** for both series. Lev is the drive. `input.wav` has to play unscaled,
  so the knob does the work. Frq has to stay put, because every CV is relative to it.
  Listen at both Mix end stops before settling them, since HP and LP may want different
  drive and one setting has to serve both.
- **the sweep range**: the span of CV 1 where the cutoff does something you'd use.
- **the Res positions**: six across the dial, with more of them where the sound changes
  fastest, plus **one held out** between two of them, to test whether the GRU fills the
  gap. The same seven for both series. Doepfer: the Wasp can't self-oscillate, so the
  top of the dial is safe ground.
- **the comparison Res**: your favourite, on LP. Its session also carries the A2 takes,
  and needs **five snapshot voltages** inside the range: where you'd actually leave the
  knob.

Write down what you like and where it turns nasty. Each session makes a `notes.md` for
that, and it's half a script for the video.

## 3. Record the series

Mix to its LP end stop. Then, once per Res position: set the knob, photograph the panel,
make and record. The comparison session first, while the Wasp is at its most settled:

```
.venv/bin/python tools/wasp_capture.py make session lp-res6 --mix lp --res 6 --cv-range=-3,5 \
    --snapshots=-2,0,1.5,3,4.5 --knobs "Lev 7, Frq noon"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res6

.venv/bin/python tools/wasp_capture.py make session lp-res0 --mix lp --res 0 --cv-range=-3,5 --knobs "Lev 7, Frq noon"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res0
```

Then Mix to its HP end stop, and the same seven Res positions with `--mix hp` and names
like `hp-res0`, without `--snapshots`.

Use the same `--cv-range` for every session in both series, or the `test` takes stop
matching. Use the
`=` with negative lists. `record` runs every take in turn, prints the peaks after each,
and skips any already recorded. Don't touch the rack while it runs. To redo one take:
`--only sweep2 --overwrite`.

## 4. Split and check

```
.venv/bin/python tools/wasp_capture.py split captures/wasp/res6
```

Each take should read **OK**. The flags:

| Flag | Meaning | Do |
|---|---|---|
| `DROPOUT` | The latency changed during the take, so samples were lost | Record that take again |
| `start and end differ` | NAM's own check: the validation passage came out differently at 0:00 and 3:01 | The Wasp drifted. Warm it up longer, then record again |
| `full scale reached` | A Wasp output clipped the ES-10 input (±12 V) | Turn Lev down, and keep it there for both series |
| `loopback only … clean` | The loopback cable isn't right | Check OUT 3 → IN 3 |

What comes out, in `captures/wasp/<session>/`:
- **`nam/`** (comparison session): one 24-bit file per snapshot and output, exactly the
  length of `input.wav` and aligned to it. Train them in NAM's GUI (`nam`), with
  `input.wav` as the input. Its simplified trainer always builds A2.
- **`gru/`**: one `.npz` per take, holding `x` (the audio as played), `cv_v` (volts at
  CV 1), `res` (the dial), `mix` (`lp` or `hp`), and `y_lphp` and `y_bp` (the Wasp's two
  outputs). This is the GRUs' data.
  Their training script is the next piece of work.
- **`listen/`**: the real Wasp on the sweeps and the held-out takes, plus the dry
  held-out riff.
- **`report.txt`** and **`split.json`**: the numbers behind the flags.

## Then: hearing the differences

- **Architecture:** A2 against the cutoff GRU, both trained on the same snapshot take.
- **The cutoff knob:** `test_snaps`. Between the snapshots A2 can only jump; the GRU
  should glide.
- **The cost of a second knob:** the LP cutoff-and-resonance GRU against the cutoff GRU,
  at the comparison Res.
- **Filling the gap:** each cutoff-and-resonance GRU, LP and HP, at the held-out Res,
  against the real Wasp.
- **The leftovers:** subtract any network's output from the real Wasp's and listen to
  what's left. That's what the network failed to learn.
