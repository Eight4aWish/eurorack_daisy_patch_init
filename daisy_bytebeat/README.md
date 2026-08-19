# daisy_bytebeat — BEAT engine

A dual-voice **bytebeat** oscillator for [`daisy_multiosc`](../daisy_multiosc/).

A bytebeat is a formula in one integer variable `t`, evaluated once per tick,
with its low eight bits taken as the sample. Because shifts and masks are
periodic in powers of two, a formula carries its own bar/beat structure — you get
rhythm and melody out of arithmetic, with no oscillator, wavetable or envelope.
Pitch is simply *how fast `t` advances*, so V/Oct is varispeed: every partial
scales together and the waveform's shape is preserved.

## Provenance

The DSP is vendored **unmodified** from the [Ogham](https://github.com/keeos-io/ogham)
module by Steven Collins (Keeos.io), MIT:

| File | What it is |
| --- | --- |
| `bytebeat_engine.{h,cpp}` | 32.32 fixed-point master phase accumulator, two voices, linear interpolation between ticks, A/B grid interpolation, Out 2 decouple/drone, hard sync |
| `formulas.{h,cpp}` | The hundred-formula bank plus an A440 reference, generated from Ogham's curated library |

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
| MOD 1 | **A** (+ MOD1 CV) | **Func 2** — voice 2, whole bank |
| MOD 2 | **B** (+ MOD2 CV) | **Grid** — A/B interpolation step |
| MOD 3 | **Func 1** — within the current bank | **Drone** — decouple Out 2 |
| Short press | cycle bank | — |

- **Banks.** The hundred formulas ship in five families of twenty, and the engine
  exposes them that way: `TEXT` `NOISE` `PERC` `RHYTM` `MELOD`, plus `REF` (the
  A440 tuning reference). Short press steps family; MOD 3 picks within it, so one
  knob covers twenty slots rather than a hundred. The position within the family
  carries across when you change bank.
- **Screen** shows the formula's own name (`BEAT:Oldskool Tune`), which is more
  use while scrubbing MOD 3 than the family name.
- **`TRIG`** (Gate In 1) is hard sync — restarts the waveform at `t = 0`.
  Roughly a fifth of the bank is one-shots that sound for between 0.4 s and about
  a minute and are silent after, so Sync is what keeps those alive. Selecting a
  formula also restarts it, so a one-shot fires when you land on it.
- **`OUT L` / `OUT R`** = voice 1 / voice 2. Both ride one master phase unless
  **Drone** is on, which freezes voice 2's rate and A/B and lets it free-run.
- **Env off by default** — this is a free-running oscillator, not a plucked voice.

## What is not ported (yet)

Ogham is more than its engine. Left out of this first pass, in rough order of
how much they'd add:

- **Lo-fi `Tone` macro** — bipolar: LPF → drive/wavefold CCW, HPF → sample-rate
  reduction CW. Ogham's fourth pot, and the biggest single loss here.
- **FX chain** — chorus → flanger → phaser, series or parallel, all DaisySP, so
  a straight lift once there are controls to spare.
- **Env Out / EOC** — envelope follower and end-of-cycle to `CV OUT` / `GATE OUT 1`.
- **Clock in and BPM estimation** — would land on `GATE 2`.
- **Internal LPG**, plucked by Sync.
- Ogham's 22-field settings menu, which the three MOD knobs can't hold; PANEL.md
  anticipates a deep menu as a final Edit-page item.

## Verified so far

Host-side, with `g++` — there is no ARM toolchain in the porting environment, so
**this has not been compiled for the target or run on hardware yet**:

- The bank is 101 slots (100 numbered + A440 at index 100), and the family
  boundaries really are 0/20/40/60/80 — the `kBanks` table was checked against
  the generated data, not assumed.
- Every formula sampled produces full-scale varying output at 1x with A=B=128.
- The A440 reference gives **exactly 440 positive-going zero crossings per
  second** at 1x, which exercises the phase accumulator, the rate mapping and the
  48 kHz assumption end to end.
- Grid interpolation (q = 0/2/8/32) neither mutes nor overflows.
- All 100 numbered formulas have an 8 kHz base rate, so 1x is one tick per 6
  output samples; above ~6x the accumulator crosses more than one tick per sample
  and starts skipping. That is Ogham's behaviour too, and the top of the knob is
  deliberately in that territory.

**Why the DC blocker.** Formula 85 *Gently Evolving* runs between −1.000 and
+0.004 — a mean near −0.5. Ogham AC-couples at the jack in hardware; this build
has to block DC in firmware, as `daisy_scanned` does, or that formula would push
a large DC offset out of the output.

## Next

1. Build for the target and bench-test — the untested half is the control
   mapping, not the DSP.
2. Check CPU headroom against the other engines, especially with Grid on (q > 1
   costs four formula evaluations per tick instead of one).
3. Consider `voct_cal.h` for calibrated 1 V/oct, as the Joy family uses; this
   follows SCAN/INTVL's uncalibrated convention for now.
