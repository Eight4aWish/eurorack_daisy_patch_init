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
| **A2 (NAM's WaveNet)** | One capture per cutoff, at one resonance | The snapshot takes in each series' Res 6 session |
| **GRU, cutoff** | The cutoff as a knob, at one resonance | The LP comparison session |
| **GRU, cutoff and resonance**, one for LP and one for HP | Both, as knobs | Every session of its series except the held-out one |

**Only the cutoff is under CV on the A-124.** Res, Lev and Mix are knobs. So the cutoff
is swept by the ES-10 inside each take, and the resonance is stepped between sessions:
**one session per Res position**. Lev and Frq stay where they are for both series; Mix
sits at its LP end stop for one series and its HP end stop for the other. The end stops
are exactly repeatable, which no mark on a dial is.

## Settings chosen on the bench, 2026-10-18

The listening tour is done. These are the settings for recording; nothing more needs
deciding by ear.

| | LP series | HP series |
|---|---|---|
| Mix | fully anticlockwise (LP stop) | fully clockwise (HP stop) |
| Frq | **0** (anticlockwise stop) | **max** (clockwise stop) |
| Cutoff range on CV 1 | **0 to +10 V** | **−10 to 0 V** |
| Lev | **10** | **10** |
| Res positions | **all 11 marks, 0 to 10**; **5** recorded but held out of training | the same |
| Sweep material | **4 minutes** per position (`--sweep-minutes 4`) | the same |
| A2 snapshots | in `lp-res6`: **+2, +3.5, +5, +6.5, +8 V** | in `hp-res6`: **−9, −7, −5, −3, −1 V**, confirmed by an HP ladder |

Why, from the ladders (recordings in `captures/wasp/tour/`):
- **The Frq knob's travel is worth about 13 V on CV 1**, and CV 1 moves the cutoff about
  an octave per volt in the middle of its range, less near the top. The ES-10 only
  reaches about ±10.7 V, so neither Frq stop covers the whole range. From the 0 stop,
  0 to +10 V runs from shut to fully open for LP, and only misses the top, where LP has
  nothing left to do. From the max stop, −10 to 0 V covers the top, where HP works.
- **The Wasp's outputs clip** at about **+5.9 / −5.1 V** (LP/HP) and **±5.5 V** (BP),
  lopsided, so the clipping adds even harmonics. At Lev 10 with a 5 V riff they reach
  it at high resonance: up to 5% of samples flattened at Res 10. That and the CMOS
  dirt are the "crazy harmonics" at Res 10: the character worth capturing.
- **Every mark, not every other one** (decided the same day): the GRU only hears
  resonance at the marks recorded and fills in the rest, and between Res 5 and 10 the
  Wasp goes from barely touching its ceiling to clipping hard. Four minutes at each of
  11 marks is about the same time as six at each of seven, spread far more evenly.
  Res 5 is recorded like any other mark but left out of training, so it sits in a
  two-mark gap: a harder test than the one-mark gaps the network will play across.
  Once it has been heard, retrain on all 11 for the network you keep.
- **A2 snapshots in both series** (decided the same day). The A2-against-GRU comparison
  only needs LP, but A2 captures are the only kind Mirth plays today: without HP
  snapshots, Mirth would get a Wasp LP and no Wasp HP until the GRU engine exists.
- **The HP ladder** (Res 6, Frq at max, −10 to 0 V) showed HP changing over the whole
  range: barely filtering at −10 V, loudest at −9 V with the resonance on the riff's
  notes, then thinner by 2–4 dB a volt, its centroid climbing about 1.5 kHz a volt near
  the top, to 10.3 kHz at 0 V. Even at 0 V it is 36 dB above the hiss, the thin top of
  the riff and not noise. The snapshots are evenly spaced across that, from the
  resonant low end to thin and bright.
- **Lev is far from linear:** Lev 5 is about 20 dB below Lev 10. The Wasp's hiss
  (about −77 dBFS) does not fall with it, so Lev 5 is clean but 20 dB closer to the
  hiss. Lev 10 keeps the character and the signal; quiet material in the takes still
  goes in clean.

## The two series

| Series | Session | Res | Takes | Time |
|---|---|---|---|---|
| **LP** | **Comparison** | 6 | `snap1`–`snap5` (NAM's `input.wav` at five cutoff CVs, 3:13 each), `sweep1`–`sweep2`, `test`, `test_snaps` | about 22 min |
| | The others | 0–4, 7–10 | `sweep1`–`sweep2` (2 min each), `test` | about 5 min each |
| | Held out | 5 | the same, never trained on until it has been heard | about 5 min |
| **HP** | `hp-res6` | 6 | the same as `lp-res6`, with HP's snapshot voltages | about 22 min |
| | The others | 0–4, 7–10, and 5 held out | `sweep1`–`sweep2`, `test` | about 5 min each |

That's about 75 minutes of takes for each series, plus turning the Res knob up a mark
between sessions. The A2-against-GRU comparison is made on LP; HP's snapshots are for
Mirth's card. It can be split
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
- **the Res positions**: chosen on 2026-10-18 as every mark on the dial, with 5 held out
  of training (see "Settings chosen on the bench"). Doepfer: the Wasp can't self-oscillate, so the
  top of the dial is safe ground.
- **the comparison Res**: your favourite, on LP. Its session also carries the A2 takes,
  and needs **five snapshot voltages** inside the range: where you'd actually leave the
  knob.

Write down what you like and where it turns nasty. Each session makes a `notes.md` for
that, and it's half a script for the video.

## 3. Record the series

Mix to its LP end stop. Then, once per Res position: set the knob, photograph the panel,
make and record. **The order is 6, evens, 6, odds, 6, LP first and then HP:**

| | Sessions | What |
|---|---|---|
| 1 | `lp-res6` | Res 6 in full; in the LP series it also carries the A2 takes |
| 2 | `lp-res0`, `lp-res2`, `lp-res4`, `lp-res8`, `lp-res10` | the other evens |
| 3 | `lp-res6-mid` | Res 6 again, the `test` take only (`--sweep-minutes 0`), 1 min |
| 4 | `lp-res1`, `lp-res3`, `lp-res5`, `lp-res7`, `lp-res9` | the odds |
| 5 | `lp-res6-end` | Res 6 a third time, `test` only |

Why this order:
- **The comparison session comes first,** while the Wasp is at its most settled.
- **After the evens you already hold every second mark,** a usable set if the sitting ends
  early.
- **Interleaving keeps slow drift from lining up with Res,** where the GRU would learn it
  as part of the resonance.
- **The three Res 6 test takes measure that drift,** and show whether it happened during
  the evens or the odds.

```
.venv/bin/python tools/wasp_capture.py make session lp-res6 --mix lp --res 6 --cv-range=0,10 \
    --snapshots=2,3.5,5,6.5,8 --sweep-minutes 4 --knobs "Lev 10, Frq 0"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res6

.venv/bin/python tools/wasp_capture.py make session lp-res0 --mix lp --res 0 --cv-range=0,10 \
    --sweep-minutes 4 --knobs "Lev 10, Frq 0"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res0

.venv/bin/python tools/wasp_capture.py make session lp-res6-mid --mix lp --res 6 --cv-range=0,10 \
    --sweep-minutes 0 --knobs "Lev 10, Frq 0"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res6-mid
```

Then Mix to its HP stop and Frq to its max stop, and the same five steps with `hp-` names.
**In HP the Wasp has a memory of a few minutes** (found 2026-10-18): its cutoff shifts a
little with what it was doing beforehand. The same test take, sample for sample, matched
itself to −24 to −32 dB whenever a pause came before it, and to −22 dB across two takes
that both came seconds after the same sweep, but only −15 to −18 dB between the two
kinds. The first A2 take after a pause creeps during its three minutes and fails NAM's
check; recorded straight after other takes it passes. So in HP, **don't start the A2
takes cold**: put a take before snap1 (a test take, or the HP ladder). The best guess,
not proven, is heat in the circuit that sets the cutoff: it warms with the cutoff
itself, and with Frq at max, the 0 V the ES-10 sends between takes is the Wasp's
highest cutoff. LP, where 0 V is the lowest, showed only a trace (its first snapshot at
0.0074 against 0.0001 for the rest). A test that would settle it: hold CV 1 at 0 V for
three minutes, then a test take; hold it at −9 V for three minutes, then another. Use
`--mix hp --cv-range=-10,0 --knobs "Lev 10, Frq max"`. `hp-res6` takes
`--snapshots=-9,-7,-5,-3,-1`; the other HP sessions take none. Those voltages were
confirmed with an HP ladder from −10 to 0 V (`make ladder --from -10 --to 0`).

Use the same `--cv-range` for every session within a series, or its `test` takes stop
matching. Use the `=` with negative lists. `record` runs every take in turn, prints the
peaks after each, and skips any already recorded. Don't touch the rack while it runs. To
redo one take: `--only sweep2 --overwrite`.

**Synth material at fixed cutoffs, for A2 through `nam-full`** (added 2026-10-19). NAM's
file does not drive the Wasp the way a sustained synth does (to David's ear it is a
guitar riff sped up and repitched), and the first A2 capture scored ESR 0.048 on NAM's
passage but 0.110 on a held-out synth riff. These takes hold the cutoff at each snapshot voltage
under the same oscillator material as the sweeps:

```
.venv/bin/python tools/wasp_capture.py make session hp-res6-fixed --mix hp --res 6 --cv-range=-10,0 \
    --snapshots=-9,-7,-5,-3,-1 --fixed-only --knobs "Lev 10, Frq max"
.venv/bin/python tools/wasp_capture.py record captures/wasp/hp-res6-fixed
```

A 3-minute warm-up take comes first, recorded but never trained on, because the Wasp
drifts on the first take after a pause; then 3 minutes per CV, back to back. About 18
minutes. `split` writes matched pairs to `fixed/` (`<take>_x.wav` and each output), kept
apart from `nam/` so the simple trainer never picks them up.

## 4. Split and check

```
.venv/bin/python tools/wasp_capture.py split captures/wasp/res6
```

Each take should read **OK**. The flags:

| Flag | Meaning | Do |
|---|---|---|
| `DROPOUT` | The latency changed during the take, so samples were lost | Record that take again |
| `start and end differ` | NAM's own check: the validation passage came out differently at 0:00 and 3:01 | The Wasp drifted: warm it up longer, then record again. **Unless the output is near the hiss:** with only about 16 dB between them, the hiss alone fails the check (`hp-res6` snap5, HP at −1 V: predicted 0.046, measured 0.048). Then nothing moved; train it with NAM's "Ignore and proceed" |
| `full scale reached` | A Wasp output clipped the ES-10 input (±12 V) | Turn Lev down, and keep it there for both series |
| `loopback only … clean` | The loopback cable isn't right | Check OUT 3 → IN 3 |

**Disk space.** A full series is large: about 2.7 GB for a session with A2 takes and
0.6 GB for each of the others once split, so both series come to about 20 GB, more than
the MacBook Air had free. `record` refuses to start a take it cannot save. `gru/` and
`listen/` can be deleted at any time and rebuilt with `split`, as long as `play/` and
`rec/` are kept; `rec/` is the one thing that cannot be rebuilt.

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

**The drift check**, once a series' three Res 6 sessions are split:

```
.venv/bin/python tools/wasp_capture.py drift captures/wasp/lp-res6 captures/wasp/lp-res6-mid captures/wasp/lp-res6-end
```

It compares each repeat's `test` take with the first: the change in level, and what
differs once level is matched. Both near nothing (a level change near 0 dB, a
difference far below −20 dB) mean the Wasp held still.

## 5. Train the A2 captures

NAM's trainer lives in its own environment, because it brings PyTorch (1.4 GB):

```
/opt/homebrew/bin/python3.12 -m venv .venv-nam
.venv-nam/bin/pip install "neural-amp-modeler==0.13.0"
brew install python-tk@3.12      # NAM imports Tk even to train without its GUI
```

Then, with the Crucial drive attached (`captures/wasp` is a link to it):

```
.venv-nam/bin/python tools/train_a2.py --fast-dev-run captures/wasp/lp-res6/nam/snap3_+5.00V_lphp.wav
.venv-nam/bin/python tools/train_a2.py captures/wasp/lp-res6 captures/wasp/hp-res6 --outputs lphp
.venv-nam/bin/python tools/train_a2.py --ignore-checks captures/wasp/hp-res6/nam/snap5_-1.00V_lphp.wav
```

The first is a one-batch test of the whole chain, seconds long. `train_a2.py` does what
NAM's GUI does (its simplified trainer, which always builds A2, then an export with the
capture's name and calibration: +20.0 dBu in, +20.8 dBu out from the ES-10's measured
volts) and writes `<session>/a2/<take>.nam`. It skips captures already trained. NAM's
checks stay on; `--ignore-checks` is for the take near the hiss.

## 6. Train the GRUs

`tools/train_gru.py` in the same environment. With `--series` it takes a whole series'
sessions and splits them itself: training on the sweeps, validation on three 9-second
blocks in every sweep take (beginning, middle, end; the first second of each unscored),
and a test of every `test` take plus whole sessions at `--holdout-res`. See it before
training:

```
.venv-nam/bin/python tools/train_gru.py --series lp --holdout-res 5 --prepare
```

That also builds a memory-mapped copy of each take (`<take>.npy` beside the `.npz`, about
3 GB for LP), which parallel runs share. The LP series' ten runs, as designed (CLAUDE.md,
"The playable GRUs"):

```
nohup caffeinate -sim tools/train_lp_playable.sh > captures/wasp/gru_runs/lp_playable.log 2>&1 < /dev/null &
```

`caffeinate -s` matters when launching over SSH: a sleeping Mac only half-wakes for the
connection, and without `-s` it drops back to sleep with the runs paused. The minutes in
a run's log stop while the machine sleeps; the file times don't.

To watch them from any computer on the network, start the status page on the training
machine and open `http://<machine>.local:8124` (the Mac mini: `Davids-Mac-mini.local`):

```
nohup .venv-nam/bin/python tools/gru_status.py > /tmp/gru_status.log 2>&1 < /dev/null &
```

Each run writes `config.json`, `split.json`, TensorBoard logs, `best.pt`, and at the end
`result.json` (validation and test ESR, per Res mark, and over the held-out Res) and
`listen/`, the chosen epoch's output on every test take at the real Wasp's gain.

## 7. How much does the Wasp vary from itself? (LP, Res 6–10)

The playable GRUs fall away above Res 7, and by ear they lack the Wasp's wildness there.
Before recording more data, measure how closely the Wasp repeats itself: if it cannot,
no network will match it sample for sample. Fifteen test-only sessions are made,
`lp-res<N>-rep1`…`-rep3` for N = 6…10, each the series' own `test` take (identical
samples), with the LP settings: Lev 10, Frq 0, Mix at LP, CV 2 at 0.

1. The Crucial drive on the recording laptop; the rack on for half an hour.
2. The rig check (step 1).
3. For each mark, 6 to 10: set Res, then
   - `rep1` and `rep2` back to back: the Wasp's short-term variation;
   - turn Res fully anticlockwise and back to the mark, then `rep3`: that plus the error of
     setting the knob by eye.

```
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res10-rep1
.venv/bin/python tools/wasp_capture.py split captures/wasp/lp-res10-rep1
.venv/bin/python tools/wasp_capture.py drift captures/wasp/lp-res10 captures/wasp/lp-res10-rep1 \
    captures/wasp/lp-res10-rep2 captures/wasp/lp-res10-rep3
```

`drift` compares each with the mark's original session (18 October), which adds a day.
Read it against the networks' test ESR at the same mark, decided before recording:
the Wasp repeating itself far better than the network means the network is short of
something, and more data at Res 6–10 is worth recording; the Wasp varying about as much
as the network's error means the network is near the floor, and listening decides;
`rep3` far from `rep2` means setting the knob by eye is a large error at that mark.

**Done 2026-10-20** (rig check identical to 18 October: latency 12,368, loopback gain
0.9155). Back to back the Wasp repeats itself to ESR 0.0003 at Res 6 and 0.0015 at 7, then
0.073, 0.39 and 0.87 at 8, 9 and 10: chaotic waveforms over a spectrum that holds within a
fraction of a dB. The knob reset adds ~0.01 at 6–7 and nothing measurable above. Full
table and reading: CLAUDE.md, "How much the Wasp varies from itself".

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
