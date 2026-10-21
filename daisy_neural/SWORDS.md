# Swords — design for review (Part A)

DRAFT, 2026-10-21, for David to review before anything is recorded or trained. Nothing
here has been built or run yet.

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

**Res is learned** if, in the judged region, the GRU-16's control test on Res reaches what
the Wasp's networks reached on the cutoff: right way 0.9 or more and amount 0.7 or more
(the Wasp's cutoff: 0.92–0.99 and 0.73–0.95), spurious movement 0.15 or less, and David
hears Res move. For comparison, the Wasp's Res, medians of `lp_cr_*`: right way 0.72,
amount 0.48, spurious 0.11.

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
