# Output noise investigation — block-rate comb

A faint comb (peaks at harmonics of the audio block rate) was audible/measurable in
the `daisy_multifx_seed` output. This is the record of how it was bisected and fixed,
so the reasoning isn't lost. **Net result: it was a hardware supply/ground problem,
not firmware.**

## Symptom

- Comb with its fundamental at the **audio block rate** (48-sample block @ 48 kHz =
  **1 kHz**), plus harmonics at 2 k / 3 k …
- Reference: the sibling [`daisy_multifx_oled`](../daisy_multifx_oled/) (same DSP/UI
  core, Patch.Init hardware) is clean to **−92 dB** — so the shared DSP is not the cause.

## Software bisection (all ruled the firmware *out*)

| Test | Result | Conclusion |
|---|---|---|
| Per-sample smoothing of k1/k2/k3/mix (fonepole) instead of block-constant | **no change** | Not the per-block control update |
| Audio block size 48 → 16 | comb fundamental moved **1 k → 3 k** | Locked to the audio block (rules out the ~1 kHz main loop and the ADC, both fixed) |
| Dry passthrough / forced silence | clean | Disturbance rides on the *processed* signal, not the raw path |
| Load test — Ladder vs Shimmer vs Crush | **−66 / −72 / −78 dB** | Does **not** track CPU/SDRAM load (Shimmer is heaviest yet mid). Tracks each effect's **signal gain** → the artifact is **multiplicative** |

A multiplicative, gain-scaled artifact = the **codec voltage reference** is being
modulated at the block rate: `out = signal × (1 + block_rate_ripple)`. Resonant Ladder
(high gain) is worst; lossy Crush is best.

## Hardware (the actual cause)

Build context: N8Synth solderable breadboard; 3× TL074 (±12 V) for audio/CV
conditioning; **Daisy powered from Eurorack +5 V**; pot/CV reference from the Daisy's
linear 3V3 out; +12 V conditioned (100 nF + 10 µF); **+5 V had no decoupling**;
measured **10–20 mV DC difference between analog and digital grounds** (shared-return
impedance).

| # | Modification | Comb (1 kHz) | Floor | Notes |
|---|---|---|---|---|
| 0 | Baseline | −66 dB (Ladder) | −108…−120 dB | |
| 1 | 100 nF + 10 µF at Daisy +5 V→GND | **no change** | — | Local bypass can't stop current pulled through a *shared* source path |
| 4 | **Dedicated buck converter → Daisy +5 V (from +12 V)** | **−90 dB** | −120…−126 dB | Removes the Daisy's bursty current from the shared rail. OLED noise also gone |

(Mods 2 "more bulk on +5 V" and 3 "star-ground" were skipped — no room / not feasible
on the N8Synth layout.)

## Conclusion

The Daisy's **per-block digital current draw on the shared +5 V rail** modulated the
codec's analog reference, producing a block-rate comb whose amplitude scaled with the
effect's output level. **Isolating the Daisy's +5 V onto its own buck supply removed
~24 dB** and put the module at the Patch.Init reference (−90 vs −92 dB).

Local pin decoupling did nothing because the problem was the shared *source path*, not
HF bypass at the pin — the lesson worth keeping for future hand builds.

## Residual / if chasing further

~−90 dB remains, most likely the still-**shared ground return** (the buck isolated the
+5 V source but its return still shares ground with the analog stage). A dedicated heavy
ground spur from Daisy GND straight to the power-entry point would be the next lever, but
it's impractical on the N8Synth boards and −90 dB is already at the reference floor.
