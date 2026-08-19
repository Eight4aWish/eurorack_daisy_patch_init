# daisy_bytebeat — BYTEBEAT engine

A dual-voice **bytebeat** oscillator for [`daisy_multiosc`](../daisy_multiosc/).

A bytebeat is a formula in one integer variable `t`, evaluated once per tick,
with its low eight bits taken as the sample. Because shifts and masks are
periodic in powers of two, a formula carries its own bar/beat structure — you get
rhythm and melody out of arithmetic, with no oscillator, wavetable or envelope.
Pitch is simply *how fast `t` advances*, so V/Oct is varispeed: every partial
scales together and the waveform's shape is preserved.

## Provenance

The DSP is vendored **unmodified** from the [Ogham](https://github.com/keeos-io/ogham)
module by Steven Collins (Keeos.io), MIT. The upstream file headers point to a
`LICENSE-firmware.txt` at *Ogham's* repository root, so the permission notice is
reproduced here as [`LICENSE-ogham.txt`](LICENSE-ogham.txt) — MIT requires the
notice to travel with the code, and a pointer to another repository does not.

| File | What it is |
| --- | --- |
| `bytebeat_engine.{h,cpp}` | 32.32 fixed-point master phase accumulator, two voices, linear interpolation between ticks, A/B grid interpolation, Out 2 decouple/drone, hard sync |
| `formulas.{h,cpp}` | The hundred-formula bank plus an A440 reference, generated from Ogham's curated library |

`lofi_tone.{h,cpp}` is **extracted and adapted** rather than vendored, from the
same project's `AudioPipeline`, where it is the `Tone` pot. Its staging and tuned
constants carry over; what changed is listed in that file's header, and the one
that matters is the pot calibration — Ogham measures its own pots because a 10k
pull-down makes them non-linear, and copying its numbers would sit the clean
deadzone left of noon on this hardware.

The formulas were **found by machine search** (a GPU search with a neural fitness
model) and are original to that project — not taken from published bytebeat
collections. Original copyright headers are intact and no line of either file has
been changed; `bytebeat_voice.{h,cpp}` is the only new code here.

Ogham's own hardware layer is not ported — it is replaced by the multiosc host.

## Control map

Universal panel — see [`../docs/PANEL.md`](../docs/PANEL.md).

| Slot | Play page | Edit page |
| --- | --- | --- |
| TUNE | Rate, 1/64x–64x (+ V/OCT) | — (host: env on/off) |
| MOD 1 | **A** (+ MOD1 CV) | **Bank** — family for voice 1 |
| MOD 2 | **B** (+ MOD2 CV) | **Func 2** — voice 2, whole bank |
| MOD 3 | **Tone** (+ MOD3 CV) | **Drone** — decouple Out 2 |
| Short press | next formula | — |

The Play page is **Ogham's four pots exactly** — Rate, A, B, Tone — and formula
selection comes off the knobs and onto the button, which is where Ogham puts it
too (its `Func` encoder). Short press walks the current family of twenty and
wraps; the Edit page picks the family.

- **Families.** The hundred formulas ship in five of twenty, and the engine
  exposes them that way: `TEXT` `NOISE` `PERC` `RHYTM` `MELOD`, plus `REF` (the
  A440 tuning reference).
- **Tone** is Ogham's lo-fi macro: CCW a 2-pole low-pass sweeping shut and then
  drive → wavefold → saturation; CW sample-rate reduction, saturation, overdrive
  and a resonant band-pass sweeping up. Clean in a deadzone at noon. Note that
  full CCW is *not* the darkest setting — the low-pass is fully shut by 60% of
  the throw and the wavefolder then re-brightens it, which is the design, not a
  bug. Ogham has no CV for Tone (it can only steal CV A or B); the spare MOD 3
  jack gives it one here.
- **Grid** (the engine's A/B interpolation) is left at Ogham's default of off.
  The engine supports it and it is worth having, but there is no knob to spare —
  it wants the deep menu PANEL.md anticipates.
- **Screen** shows the formula's own name, which is more use while scrubbing
  MOD 3 than the family name. The panel is ten characters wide, so `BYTEBEAT`
  keeps the title line and the formula gets its own line under the knob grid,
  truncated to ten (`Oldskool T`). No two of the 101 names collide there. The
  Edit page names the family on the same line instead — `EDIT:NOISE`, exactly
  ten — so you can see which twenty the short press is about to walk.
- **`TRIG`** (Gate In 1) is hard sync — restarts the waveform at `t = 0`.
  Roughly a fifth of the bank is one-shots that sound for between 0.4 s and about
  a minute and are silent after, so Sync is what keeps those alive. Selecting a
  formula also restarts it, so a one-shot fires when you land on it.
- **`OUT L` / `OUT R`** = voice 1 / voice 2. Both ride one master phase unless
  **Drone** is on, which freezes voice 2's rate and A/B and lets it free-run.
- **Env off by default** — this is a free-running oscillator, not a plucked voice.

## What is not ported

**Deliberately out of scope**, not a backlog:

- **FX chain** — chorus → flanger → phaser, series or parallel. A straight
  DaisySP lift, but the rack does modulation and delay better in dedicated
  hardware, and it would cost knobs BYTEBEAT has better uses for.
- **Internal LPG**, plucked by Sync — same reasoning.

Ogham puts these inside the module because it is a standalone 10 HP voice with
nowhere to send Out 1 and Out 2. Here the outputs go straight into the rack, so
the module only has to be a good source.

**Still worth doing.** Note that all three want *jacks*, not knobs — `GATE 2`,
`GATE OUT 1` and `CV OUT` are all currently unused, so none of them costs
anything on the panel:

- **Env Out** — the envelope follower to `CV OUT`.
- **EOC** — end-of-cycle to `GATE OUT 1`, which pairs with the one-shot formulas.
- **Clock in and BPM estimation** — would land on `GATE 2`, and is the other half
  of what makes the rhythmic families useful against a sequencer.

**Grid** (A/B interpolation) is implemented in the engine and switched off for
want of a control; it wants the deep menu PANEL.md anticipates as a final
Edit-page item. That menu is also where the rest of Ogham's 22 settings fields
would live, if any of them turn out to be wanted.

## Verified so far

It now **builds for the target** (2026-08-19, `daisy_multiosc` at 122,280 B —
1.5% of QSPI, so size is not a constraint), and the checks below pass host-side
with `g++`. What remains unverified is **the control mapping on hardware**: no
knob, jack or button in the map above has been touched on the bench.

- The bank is 101 slots (100 numbered + A440 at index 100), and the family
  boundaries really are 0/20/40/60/80 — the `kBanks` table was checked against
  the generated data, not assumed.
- Every formula sampled produces full-scale varying output at 1x with A=B=128.
- The A440 reference gives **exactly 440 positive-going zero crossings per
  second** at 1x, which exercises the phase accumulator, the rate mapping and the
  48 kHz assumption end to end.
- Grid interpolation (q = 0/2/8/32) neither mutes nor overflows.
- Tone is a true bypass at noon (bit-identical), and stays finite and inside full
  scale across all 101 knob positions — including under a continuous knob sweep
  with filter state carried across, which is the case that actually rings a
  resonant SVF.
- All 100 numbered formulas have an 8 kHz base rate, so 1x is one tick per 6
  output samples; above ~6x the accumulator crosses more than one tick per sample
  and starts skipping. That is Ogham's behaviour too, and the top of the knob is
  deliberately in that territory.

**Why the DC blocker.** Formula 85 *Gently Evolving* runs between −1.000 and
+0.004 — a mean near −0.5. Ogham AC-couples at the jack in hardware; this build
has to block DC in firmware, as `daisy_scanned` does, or that formula would push
a large DC offset out of the output. Measured: −0.500 raw → +0.00002 blocked.

**Why the output limiter.** Two stages overshoot. Tone's overdrive and resonant
band-pass reach **1.88** from the engine's ±1, and the DC blocker — a high-pass —
reaches **1.25** on a formula with a large offset and fast transitions. Ogham can
afford the first because it halves its output afterwards to suit a hot analog
stage; here both would simply clip at the codec. A soft knee at 0.7, applied
inside Tone and again as the last stage before the jack, leaves everything below
it untouched and holds the ceiling at 1.0. Neither was predictable by reading the
code — both came out of measurement.

## Next

1. Bench-test the control map — the untested half is the panel, not the DSP.
2. Confirm CPU headroom by ear. Tone is the new cost: measured host-side it is
   about 2.7x the clean path at full CCW and 1.9x at full CW, against a budget of
   10,000 cycles per sample (480 MHz / 48 kHz). The worst case is roughly six
   `tanhf` calls per sample — two voices past the limiter knee, each through a
   saturator and the output knee — which should land near a quarter of the
   budget, but that is an estimate from a host build, not a target measurement.
   Dropouts or crackle at the extremes of MOD 3 is the thing to listen for.
3. Consider `voct_cal.h` for calibrated 1 V/oct, as the Joy family uses; this
   follows SCAN/INTVL's uncalibrated convention for now.
