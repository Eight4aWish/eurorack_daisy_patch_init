# Swords — design for review (Part A)

Reviewed by David and recorded 2026-10-21: step 0 and the session `sw-a` (steps 1 and 2)
are done; training waits on the Mac mini.

## The question

The Wasp's cutoff + Res GRUs learned the cutoff well and Res weakly, even at Res 6–7 where
the Wasp repeats itself almost exactly (take to take ESR 0.0006–0.03, the networks
0.05–0.21; CLAUDE.md, the stability table). Res on the Wasp is a panel knob, so every Res
mark was a separate session set by hand. On Swords, Res is under CV.

**With Res swept by CV, continuously and together with the cutoff, in one session, does a
cutoff + Res GRU learn Res, in the region where Swords repeats itself?**

- If it does: the Wasp's weak Res most likely came from the recording method (marks set by
  hand, one session each), not from the network.
- If it does not: the limit is the network or the training, even with continuous Res.
- Either way only suggestive for the Wasp: Swords is a different filter. Where Swords
  goes wild (self-oscillation) a network is expected to fail, as the Wasp did at Res 9–10,
  so that region is recorded but not judged.

Part B (mode and drive under CV as well) waits for this answer.

## The module (Behringer's quick start guide, V1.0, checked 2026-10-21)

Filter 1 only. Per filter: Drive (1) with Drive CV (3, 0 to +8 V); Response (2, knob
only: soft clip fully anticlockwise, wavefolding clockwise); Mode (4, LP anticlockwise,
BP noon, HP clockwise) with Mode CV (5, 0 to +8 V); Resonance (7) with its CV
attenuverter (8) and Resonance CV In (9, 0 to +8 V), "at high levels the filter
self-oscillates"; Freq (10) with its CV attenuverter (11) and Frequency CV In (12, 0 to
+8 V); In (13); V/OCT (14). Shared: Shift (29), Routing (30) with its CV, and three
DC-coupled outputs, 1 kΩ: Filter 1 Out (33), Main Out (34), Filter 2 Out (35). Mutable's
Blades manual: cutoff 8 Hz to 18 kHz; drive 0 dB at 11 o'clock. The guide's
specification table extracts scrambled on ManualsLib (it gives ±8 V for several CV
inputs where the control pages say 0 to +8 V), so the ranges that matter are measured in
step 0. The inventory's "self-oscillates above ~+3 V" has no source found; step 0
measures it.

## Panel and patch (Part A)

| Control | Setting | Why |
|---|---|---|
| Drive (1) | 11 o'clock (0 dB) | the input stage near-linear; Drive CV (3) unpatched |
| Response (2) | fully anticlockwise (soft clip) | fixed; matters little at 0 dB |
| Mode (4) | fully anticlockwise (LP) | Mode CV (5) unpatched |
| Freq (10) | set in step 0 | so the cutoff CV covers the audible range |
| Freq CV attenuverter (11) | noon (off) | cutoff comes in through V/OCT; Freq CV (12) unpatched |
| Resonance (7) | fully anticlockwise | Res comes from CV alone |
| Resonance CV attenuverter (8) | fully clockwise | CV at full scale |
| Shift (29), Routing (30) | off, fully anticlockwise | filter 2 unused; the recording is Filter 1 Out |

| ES-10 | Swords | |
|---|---|---|
| OUT 1 | In (13) | the audio, peaks at 5 V as for the Wasp |
| OUT 2 | V/OCT (14) | cutoff |
| OUT 3 | ES-10 IN 3 | the loopback, for latency, as for the Wasp |
| OUT 4 | Resonance CV In (9) | Res |
| IN 1 | Filter 1 Out (33) | the recording |

## Steps

**Step 0, done 2026-10-21** (`tools/swords_capture.py make cal`, `cal`; Freq knob fully
anticlockwise, the rest as the panel table). Latency 12,370 samples. **Cutoff tracks V/OCT
at 0.94–0.96 octaves per volt, 0 V about 9–10 Hz** (Blades' bottom is 8 Hz), checked up to
+10.5 V (8.9 kHz): +3 V 65 Hz, +5 V 247 Hz, +8 V 1,863 Hz, +10 V 6,640 Hz. (The first fit
read 0.52: steps whose cutoff is under 20 Hz had pulled it down; it now uses only steps
whose passband shows.) Passband -1.2 dB. **Self-oscillation, no input, Res CV rising then
falling:** at V/OCT +1 V it starts at 5.43 V and stops at 3.44 V; at +4 V, 4.56 and 3.88;
at +7 V, 4.22 and 4.04. It starts at less Res the higher the cutoff, and stops lower than
it starts (most at low cutoffs). Oscillating, it reaches about 7–9 V peak, within the
ES-10's range. The riff at 5 V peak, cutoff about 930 Hz: 3.71 V peak out. **Chosen for the
session:** V/OCT +2.5 to +10.5 V (about 45 Hz to 9 kHz) with the knob at its end stop; Res
CV 0 to 6 V (just past the onset everywhere, so most of the material is below it); repeat
takes at Res 0, 1, 2, 3, 3.5, 4, 4.5 and 5.5 V. Session `sw-a`: 44 takes, 84.8 minutes,
checked end to end (split, region) on simulated recordings first.

**0. Calibrate (attended, about 15 min).** Measure, before any number below is fixed:
the cutoff against V/OCT (the Wasp's fit, from where the random part peaks, does not
apply; here from a noise or sweep input at held voltages); Res CV's working range (where
resonance starts to show, and where the filter self-oscillates with no input); the input
level against the drive LED.

**First play (David, 2026-10-21, by hand, Res CV unpatched, Response at 12 o'clock):**
where it starts to self-oscillate, Freq against Res in clock positions: Freq 9 o'clock,
Res 4; Freq 12, Res 3; Freq 3, Res about 2:30. The onset moves with the cutoff: the higher the cutoff,
the less Res it takes. So the edge of the repeatable region is a line across cutoff and
Res, not a single Res value.

**It moves with Response too** (David, same day). With Response fully anticlockwise, the
soft-clip end and Part A's setting: Freq fully clockwise, no self-oscillation at all;
Freq 3 o'clock, Res 3; Freq 12, Res 3; Freq 9, Res 4. So the drive stage is not only on
the input, as the manuals read: with the input silent, Response still moves where the
filter oscillates, which suggests the clip/fold stage interacts with the resonance loop.
The schematic would settle how (Mutable's `blades_v40.sch` is a binary Eagle file, not
read). For the design: Response is held fully anticlockwise throughout Part A and
recorded with every capture, and Part B's drive matters for the resonance as well as the
dirt.

**1. Where it stops repeating (about 20 min).** The `test` take (the riff, cutoff
stepping) recorded three times back to back at each of 6–8 Res CV values across the range,
self-oscillation included. **The judged region is fixed by a rule set now:** each pair of
a cutoff step (one hold of the `test` take, as the Wasp's ladder measured per hold) and a
Res CV value whose back-to-back take-to-take ESR over that hold is 0.02 or less (the Wasp,
whole take: 0.0015 at Res 7, 0.073 at Res 8). Outside it, recorded but not judged.

**Steps 1 and 2 recorded 2026-10-21** (`sw-a`, 44 takes, unattended, no warnings; Filter 1
peaks −2.8 to −1.8 dBFS, so no clipping; every take aligned, lag 12,369–12,376 samples,
loopback residual −53 to −60 dB). **Where Swords repeats itself** (`region`), take-to-take
ESR per hold, V/OCT across, Res CV down:

| Res CV | +2.5 V | +4.1 V | +5.7 V | +7.3 V | +8.9 V | +10.5 V |
|---|---|---|---|---|---|---|
| 0–3.5 V | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| 4.0 V | 0.0000 | 0.0000 | 0.0001 | 0.0002 | 0.0006 | 1.15 |
| 4.5 V | 0.72 | 0.045 | 0.24 | 0.21 | 0.74 | 1.40 |
| 5.5 V | 0.011 | 0.0015 | 0.020 | 0.20 | 1.19 | 2.03 |

Below Res CV 4 V Swords repeats itself to the rig's floor, far closer than the Wasp ever
did (0.0006–0.03 at Res 6–7). The edge is the self-oscillation onset step 0 measured: at
4.0 V the top cutoff has already gone, at 4.5 V everything has (the onset with hysteresis,
where it starts and stops). At 5.5 V the two lowest cutoffs repeat again (0.011, 0.0015):
oscillating steadily, apparently held in step by the input; judged by the rule, though
they are islands past the onset. **Held-out band: Res CV 1.8–2.2 V**, by the rule set
before the recording (a tenth of the judged span, 0–4 V, at its middle); it holds the
2.0 V test takes.

**2. Part A (about 70 min, unattended).** About 60 minutes of training material in two
halves of 30, each from its own seed, the same generator as the Wasp's sweeps
(`sweep_audio`: notes, glides, noise bursts, phrases, rests, at several levels), so the
material is like for like; the cutoff and Res CVs each a slow independent walk across
their whole ranges. Then the `test` take at a few fixed Res values, and three takes of it
back to back at one judged value, for the stability check. More recording, but the
comparison stays like for like (David, 2026-10-21): see the two arms below.

**Held out**, as Res 5 was for the Wasp: one band of Res CV inside the judged region, about
a tenth of its Res span near the middle, chosen after step 1 and cut out of the training material
wherever Res passes through it. Validation as for the Wasp (blocks at the beginning,
middle and end of each take), early stopping on it; the `test` takes never trained on.

## Networks and the verdict

The Wasp's first recipe unchanged, so only the data differs: GRU-16 and GRU-24, seeds 1–3,
ESR, the same schedule and early stopping, clip at 10. Inputs: audio, cutoff volts, Res
volts, each per sample. Two arms from the same session, both with the held-out band cut
out and judged the same way:

- **Matched** (the filter-against-filter answer): one 30-minute half, about the amount of
  sweep material the Wasp's cutoff + Res networks had (31 minutes).
- **All**: both halves, 60 minutes. Whether more material helps Res, which the Wasp's
  series could not say.

Twelve runs; the Mac mini takes six at a time, one thread each, so overnight.

**Run on the laptop, 2026-10-21** (the mini refused the key; same M4, 4P+6E, 16 GB, but
fanless), held-out band 1.8–2.2 V: 46 minutes of training for the two halves, 23 for
one, about 50 s an epoch. **The validation curve is mostly self-oscillation:** of the
matched arm's ten validation blocks, 59% of the time and 94% of the output energy is
above Res CV 4 V, where Swords does not repeat. So `valid/ESR` (about 0.85 at epoch 41,
against the Wasp's final 0.16) and the early stopping it drives say little about the
judged region; only the control test does. Kept as designed (David): the recipe stays the
Wasp's. If Res fails, early stopping on the judged region only is the first thing to try.

**Verdict (2026-10-22): Res is not learned, in either arm.** The 60-minute arm ran on the
Mac mini (same M4, same PyTorch and NumPy), alongside the laptop's 30. No run passes;
the cutoff is learned in all twelve (right way 0.95–1.00, amount 0.88–1.00, no spurious
movement).

| Medians (range over 6 runs) | 30 min (matched) | 60 min (all) | Wasp `lp_cr_*` |
|---|---|---|---|
| held-out ESR, Res 2 V test take | 0.012 (0.007–0.019) | 0.006 (0.004–0.011) | 0.045 (Res 5) |
| Res right way (pass 0.9) | 0.75 (0.61–0.77) | 0.61 (0.48–0.75) | 0.72 |
| Res amount (pass 0.7) | 0.69 (0.38–0.84) | 0.35 (0.20–0.46) | 0.48 |
| Res spurious (pass 0.15) | 0.11 (0.06–0.22) | 0.07 (0.04–0.24) | 0.11 |

- **Filter against filter:** at the Wasp's amount of material, continuous Res CV gives the
  same right way as the Wasp's marks set by hand, somewhat more amount. By the design's
  reading, the Wasp's weak Res was not mainly the recording method: the limit is the
  training or the network.
- **More material made the waveform better and Res worse.** Doubling it halved the
  held-out error but halved the amount, ranges barely overlapping: the networks match
  Swords' sound more closely while moving less with Res.
- **The seeds scatter on Res but not on the score trained on** (30 min: validation ESR
  0.69–0.75, amount 0.38–0.84), and neither ESR predicts which seed gets Res right.
- **The leading suspect: what training spends its effort on.** Above Res CV 4 V, where
  Swords oscillates and repeats nothing, is a third of the training time and 75–77% of its
  energy; the ESR loss is energy-weighted, so Res 0–4 V, the judged region, is a quarter
  of what training sees, and early stopping watches a validation set that is 94%
  oscillation. A hypothesis, not shown. Its test: re-weight (each segment scored on its
  own, or training and early stopping on Res 0–4 V only), same data, same seeds; if it is
  right, the seeds should agree on Res as well as pass. Not yet designed in detail or run.
- Still to do for the verdict as written: David hears Res move (listening files per run
  in `gru_runs/*/listen`).

**Res is learned** if, in the judged region, the GRU-16's control test on Res reaches what
the Wasp's networks reached on the cutoff: right way 0.9 or more and amount 0.7 or more
(the Wasp's cutoff: 0.92–0.99 and 0.73–0.95), spurious movement 0.15 or less, and David
hears Res move. For comparison, the Wasp's Res, medians of `lp_cr_*`: right way 0.72,
amount 0.48, spurious 0.11.

## Fixed Res (recorded and queued 2026-10-22)

David's idea after the verdict: train at each Res and morph between them, the Wasp's
per-mark method plus a blend.

**Recorded** (`sw-b-*`, 04:45–05:46 after a 30-minute warm-up, panel and riff as `sw-a`):
eight sessions at fixed Res CV 0, 0.5, 1, 1.5, 2.5, 3, 3.5 and 4 V, each 6 minutes of
sweeps from its own seed (5.1 minutes to train on after validation) and one test take at
its Res, in a mixed order (0, 2.5, 1, 4, 3.5, 0.5, 3, 1.5) so drift cannot line up with
Res. Res 2 V only as three 1-minute test takes, at the start, middle and end. All 27 takes
clean, peaks −9.2 to −2.0 dBFS, aligned (lag 12,369–12,376). **Drift:** the 2 V takes,
per hold against the middle one: the start 0.0002–0.0049 (0.0014 whole), the end
0.0000–0.0003: Swords still settling for the first quarter hour, then steady. Yesterday's
2 V take against today's: 0.0034 whole, 0.0115 at the lowest cutoff, below the networks'
own error. The blend is judged against today's takes.

**Queued on the mini** (`tools/queue_swords_fixed.sh`, after nothing, four places
alongside the other queues): GRU-16, audio + cutoff + Res (constant within a session, so
both families have the same architecture), seed 1, Part A's recipe otherwise.
- **Independent:** `swf_ind_r*`, one per session from random weights.
- **Shared:** `swf_parent` on all eight sessions together, then `swf_ft_r*`, a copy of it
  trained on each session at a fifth of the learning rate (`--init-from`, `--lr 1e-3`),
  so neighbours stay close enough to blend by weights.
- **Judged on:** each network against Swords at its own Res; the family's Res response
  with Part A's control test cells; and at the held-out 2 V, the 1.5 and 2.5 V networks
  blended (independent: outputs crossfaded; shared: outputs crossfaded and weights
  averaged) against today's 2 V takes, beside the nearest single network.

## Results, 2026-10-22: Res 0–4 V, and fixed Res

Medians with the range over seeds; a difference counts only where the ranges separate.

**Res 0–4 V (`sw4_*`, `--res-max 4`, the same sessions, seeds and recipe as Part A).** On
`sw-a`'s takes (Part A's cells):

| | 30 min, full range | 30 min, 0–4 V | 60 min, full range | 60 min, 0–4 V |
|---|---|---|---|---|
| Res right way | 0.75 (0.61–0.77) | 0.83 (0.79–0.89) | 0.61 (0.48–0.75) | 0.78 (0.71–0.88) |
| Res amount | 0.69 (0.38–0.84) | 0.71 (0.66–0.73) | 0.35 (0.20–0.46) | 0.71 (0.39–0.82) |
| Res spurious | 0.11 | 0.11 (0.08–0.20) | 0.07 | 0.11 (0.04–0.16) |
| held-out ESR, 2 V | 0.007–0.019 | 0.0014–0.0048 | 0.004–0.011 | 0.0020–0.0049 |

Clear of the scatter: right way in the 30-minute arm, amount in the 60-minute arm, and the
held-out error in both; the 30-minute arm's amount spread collapsed (0.38–0.84 to
0.66–0.73). So the energy explanation holds for Swords: trained without the oscillation,
Res is learned far better, and more material no longer flattens it. No run passes on
`sw-a`'s takes (right way best 0.89).

**Fixed Res (`swf_*`, one seed each).** Each network at its own Res is very close to
Swords (ESR 0.0001 at 0–1 V, 0.001–0.014 to 3.5 V; 0.32 at 4 V, where the test take
oscillates at the top cutoff). Judged with the same 65 cells on this morning's `sw-b`
takes, beside the Res 0–4 V networks:

| | right way | amount | spurious |
|---|---|---|---|
| Res 0–4 V, 12 runs | 0.86 (0.74–0.92) | 0.87 (0.68–0.92) | 0.11 (0.05–0.19) |
| independent family | 0.91 | 0.95 | 0.24 |
| shared family | 0.82 | 0.92 | 0.30 |
| parent alone | 0.74 | 0.92 | 0.08 |

Two Res 0–4 V runs pass all three marks on these takes (`sw4_matched_gru16_s2`,
`sw4_matched_gru24_s3`: 0.92 / 0.92 / 0.11 and 0.10), not on `sw-a`'s: borderline, the
verdict depends on the takes. The families sit inside the Res-input range or at its top,
and spurious is above it: no detectable advantage. **At the held-out 2 V** (Swords against
itself 0.0001): the 1.5 and 2.5 V independent networks crossfaded 0.0032 (alone 0.038 and
0.060); shared crossfaded 0.0050; the parent 0.0043–0.0047; the Res 0–4 V networks
0.0035–0.0076. Crossfading sits at the best end of that range, inside it. **Averaging
the shared family's weights failed** (0.071, worse than either alone): copies trained on
from one parent at a fifth of the rate did not stay close enough to blend.

**So:** fixed-Res recording plus a crossfade matches a single Res-input network trained on
Res 0–4 V, and does not beat it. For Mirth the single network is the candidate (one
network; a crossfade needs two GRU-16s at once, over the budget). Its Res input is volts
0–4, so Mirth would need a per-file Res range before it could go on the card.
Evaluation: `tools/swords_fixed_eval.py`, `captures/swords/gru_runs/swf_eval.txt` (mini).

## Above the threshold (measured 2026-10-21, `tools/swords_osc.py`)

For Part B or later: what Swords does past the onset, from step 0's Res ramps (no input)
and the `sw-a` test takes (the riff, three takes back to back).

- **One clean, stable tone at the filter's own frequency.** At V/OCT +4 V it oscillates at
  166.5–169 Hz, at +7 V at 1,260–1,277 Hz: about 1.33 times the −3 dB point at Res 0
  (128 and 940 Hz), tracking V/OCT like the cutoff. Res moves the pitch only 23–26 cents
  from the onset to Res CV 8 V. The peak is narrower than a 2 s window can show (−3 dB
  width 0.69 Hz, the window alone 0.72); the pitch wanders 0.1 cents rms in 50 ms frames.
- **Its level and harmonics grow with Res.** From 1.9–2.9 V peak at the onset to 7.4–7.5 V
  at Res CV 8 V. At the onset almost a pure sine (H2 −68 to −71 dB, H3 −77 to −80); at the
  top odd harmonics, the mark of a symmetric saturation in the loop: H3 −29, H5 −38, H2
  −50, H4 −67 dB, the same at both cutoffs.
- **With the riff, only the phase is random.** Where the waveform does not repeat
  (take-to-take ESR 0.2–2.0 at Res 4.5 and 5.5 V), the magnitude of a 43 ms STFT does:
  ESR 0.000–0.026; band levels over the whole hold differ by 0.0–0.5 dB; the strongest
  peak is the same frequency in all three takes to 1 Hz (1,564, 4,515, 12,557 Hz at the
  top three holds). What is heard repeats; where each take's oscillation starts in its
  cycle does not.

- **The input nudges the pitch; it does not snap it** (`tools/swords_pull.py`, 170 ms
  frames, Res 4.5 and 5.5 V). David's hunch was that the input's pitches pull the
  oscillation around a set of frequencies. The pitch does move: over 35–75 cents at a
  home of 180 Hz, about 30 at 530 Hz, a few at 1.5 kHz, about 1 above. The movement is
  the input's doing: the three takes trace the same pitch track to 0.2–6 cents. But it
  lands on an input partial no more often than chance (11–13% of frames against 14% at
  180 Hz; 34–37% against 34% at 530 Hz) and barely leans towards the strongest nearby
  one (r about 0.2 at 180 Hz, 0 at 530 Hz). Louder input, slightly flatter: about
  −2 cents per dB at 180 Hz (r −0.2 to −0.4), what a clip in the loop does, and only
  part of it. One riff, whose partials rarely come close to the oscillation; a slow sine
  glide through it at a few levels would show whether there is a capture range. David's
  guess: it shifts rather than snaps, depending on the relative powers.

- **The glide test (2026-10-22, `tools/swords_glide.py`, session `sw-glide`): it locks, and
  the lock widens with the tone's level.** Res CV 5.5 V, V/OCT 4, 6, 8 V (free pitch 165,
  640, 2,468 Hz, 2–3% under step 0's map; 14.2 dBV, about 7 V peak); a sine gliding two
  octaves around it, up and down over 30 s each, at 0.2, 1 and 3 V peak. Lock range, cents
  from the free pitch, the same up and down:

  | Sine (dB under the oscillation) | 4 V | 6 V | 8 V |
  |---|---|---|---|
  | 0.2 V (31 dB) | about ±20 | about ±20 | −21 to +23 |
  | 1 V (17 dB) | −158 to +148 | −156 to +147 | −150 to +140 |
  | 3 V (8 dB) | −624 to +476 | −626 to +472 | −603 to +452 |

  Inside the range it snaps onto the tone; outside it is pulled 16–39 cents. The range grows
  with the tone's level, is the same in cents at every cutoff (a fixed fraction of the
  pitch), is a little wider downward at 3 V, and has no hysteresis (it grabs and lets go at
  the same points either way). David's guess ("shift rather than snap, depending on the
  relative powers") is half right: both, the balance set by the level. About four times
  stickier than a driven van der Pol oscillator standing in for it (±37 cents at 1 V), on
  which the reading was checked first. For the grey-box, the targets: the lock range against
  the level, constant in cents, no hysteresis, the pull outside.

**So it is learnable, but not by a sample-by-sample loss.** **David's call (2026-10-21):
a grey-box addition** (option 2), helped by the oscillation not being purely random. The
options as written:
1. **Same GRU, a phase-blind loss** (multi-resolution STFT, mixed with ESR for below the
   threshold). Smallest change, same data, same cost on Mirth. Risk: the GRU has to become
   a sustained, clean oscillator over nine octaves, a known weak spot for RNNs.
2. **Grey-box: the filter's own structure** (a state-variable filter, two integrators and
   a feedback loop, with a learned saturation in the loop and learned cutoff and Res maps,
   as in differentiable-filter work: Kuznetsov, Parker and Esqueda 2020). Oscillation, its
   pitch, its level set by the saturation, the odd harmonics and plausibly the hysteresis
   are what that structure does, rather than something to learn. Far cheaper on the
   Daisy than a GRU. New training code; Blades' schematic gives the structure.
3. **Hybrid, as the Wasp: the GRU below, a measured oscillator above.** A sine at the
   measured pitch map, level and harmonics tabled against Res and cutoff, onset and stop
   lines from step 0, a random start phase. Cheapest and most predictable; the weak point
   is how it meets the input (the islands at 5.5 V where the input pulls it into step).

## FLT6: Swords with its oscillation (built 2026-10-23)

David's shape: FLT5's capture plus a layer, as FLT3 is the Wasp plus its granular layer,
not a circuit model. Nothing new was recorded; every constant is from a measurement already
made.

**What it is.** FLT5's GRU (`sw4_all_gru16_s3`, Res CV 0–4 V, where Swords repeats itself)
with an **oscillator layer** (`nam/osc_layer.h`) added to its output. The dial reads Swords'
Res CV: 0–10 is 0–4 V as on FLT5, 10–11 is 4–6 V (`sw-a`'s range). The GRU's Res stays at
4 V past 10; the layer sees the Res CV itself, so at the top cutoffs, where Swords starts
lower, it comes in just under 10 (the onset line reaches about 3.8 V at V/OCT 10.5,
extended from the knots at 1, 4 and 7 V). No feedback.

**The layer, part by part:**

| Part | Model | From |
|---|---|---|
| When it sounds | on above the onset line, off below the stop line, both piecewise linear in V/OCT through the measured knots (start 5.43 / 4.56 / 4.22 V, stop 3.44 / 3.88 / 4.04 V at V/OCT 1 / 4 / 7), so it starts lower at higher cutoffs and has hysteresis; with the input sounding it starts by 4.45 V wherever the no-input onset is higher | step 0; `sw-a`'s riff takes (every take at 4.5 V oscillates, none at 4.0 V below the top cutoff) |
| Its pitch | 169.1 Hz at V/OCT 4, 0.978 octave per volt bending slightly at the top: one quadratic through the glide's three free pitches and `sw-a`'s six, within 44 cents of each (about the drift between the two days); +5 cents per volt of Res CV | `sw-glide`; `sw-a`; step 0 |
| Its level | 7.0 V peak just past the onset, rising to 7.45 V at 8 V; ducked by the input's level, 1 / (1 + 0.276 A) with A the input's peak volts, because the input and the oscillation share Swords' clip | step 0; `sw-a` (the oscillation 7.5 dB down with the riff at 5 V peak) |
| Its shape | a sine with H2 at −51 dB and odd harmonics growing past the onset: H3 −34.5 → −29.6 dB, H5 −42.4 → −38.2 dB | step 0 |
| Locking | Adler's model on the input's part near the pitch only: the input mixed down at the oscillator's phase and low-passed (within 0.6 of the pitch) gives that part's level A and phase; it locks when the detuning is under 0.075 A^1.2 of the pitch in Hz, the same up and down in Hz as measured (−624 / +476 cents at 3 V) | `sw-glide` |
| Start | a random phase each time it starts | `sw-a` repeat takes |

**Checked on the desktop** against the marks set in the design: FLT5's network plus the
layer, through `tools/osc_host` (the firmware's own C++), with `tools/osc_layer.py check`
(output in `captures/swords/osc_check.txt`).

| Check | Mark | FLT6 | Swords | Verdict |
|---|---|---|---|---|
| Free pitch at V/OCT 4 / 6 / 8 | within 2% | 169.1 / 650.4 / 2,460 Hz | 164.9 / 640 / 2,468 | +2.5%, +1.6%, −0.3%: 4 V misses by half a percent (one curve through both days' points) |
| Lock range, 1 V peak | within a quarter | −170 to +180 / −172 to +178 / −148 to +154 cents | about ±150 | 0–20% wide: passes |
| Lock range, 3 V peak | within a quarter | −753 to +569 / −725 to +567 / −641 to +497 | −624 to +476 | up to 21% wide: passes |
| Lock range, 0.2 V peak | — | — | about ±20 | not readable: inside the reading's ±15-cent margin for both |
| Hysteresis | none | up and down sweeps within 13 cents | none | passes |
| Pull outside the lock | no mark | 5–30 cents at 0.2 V, 62–159 at 1 and 3 V | 16–39 | pulls harder than Swords at 1 and 3 V |
| Onset and stop, no input | within 0.2 V | starts exact, stops 0.05–0.06 V low | | passes |
| Level, no input | within 2 dB | 0.1–0.2 dB under | | passes |
| Pitch, riff takes at 4.5 and 5.5 V | no mark | within 0.3–1.6% at every hold | | |
| Oscillation level, riff takes | no mark | within 2.5 dB at V/OCT 2.5 and 4.1; 2–8 dB under above | | ducked too hard at the higher cutoffs |
| Band levels, riff takes | within 2 dB | 1.2–1.5 dB at V/OCT 8.9 and 10.5; 2.7–6.3 dB below | | passes at the top two holds only |

Against FLT5's network alone (Res capped at 4 V) on the same riff takes, the layer brings
the band error down by 0.7–2.8 dB at every hold but the top one (4.5 V: 6.4 → 5.7, 4.6 →
3.2, 5.3 → 4.0, 4.8 → 2.6, 2.6 → 1.2 dB; 5.5 V: 7.3 → 6.3, 5.7 → 3.9, 7.3 → 4.8, 5.9 →
3.1, 3.1 → 1.1 dB). At V/OCT 10.5 the network alone was already within 1 dB and the layer
adds 0.3–0.8 dB.

**What it leaves out:** how the input and the oscillation share Swords' clip beyond one
ducking factor (the level shortfall at the higher cutoffs is that), and the islands at
5.5 V where the input holds the oscillation's phase steady (the locking covers part).
Still to judge on the module: its CPU (the FILTERS page's cycle reading) and David's ear.

**On the card:** `f6_SWORDS_OSC.grnb` (panel name SWORDS_OSC): FLT5's file with a version-2
controls section, written by `tools/grnb_controls.py --osc`: dial 0 / 10 / 11 → network Res
0 / 4 / 4 V and layer Res CV 0 / 4 / 6 V, no feedback, and the layer's 34 constants
(`captures/swords/osc_params.f32`). It needs the firmware of 2026-10-23; older firmware
cannot read that section and gives the file the Wasp LP's controls.

## To build after review

- `tools/wasp_capture.py`: a fourth play channel (Res CV on ES-10 OUT 4), a Res walk for
  the training material, the repeat takes, the held-out band, recordings under
  `captures/swords/` (the laptop's own disk: about 1–3 GB, then copied to the mini).
- `tools/train_gru.py`: Res as a per-sample input (`res_v`) instead of one number per take.
- `tools/control_response.py`: Res cells in volts, restricted to the judged region (per
  cutoff step).
- Mirth's FILTERS bank needs no change: the file's input scales carry volts.

## Open for David

1. The judging thresholds: take-to-take ESR 0.02 per cutoff step for the judged region; right way 0.9,
   amount 0.7 and spurious 0.15 for "learned".
2. ~~Thirty minutes, as the Wasp had?~~ Decided (David, 2026-10-21): record more, keep
   the comparison at the Wasp's amount; two arms, matched and all.
