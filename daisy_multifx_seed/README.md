# daisy_multifx_seed

The shared [`multifx_core`](../common/multifx_core/) multi-FX on a bare **Daisy Seed**
with a home-built Eurorack front end — the sibling of [`daisy_multifx_oled`](../daisy_multifx_oled/)
(Patch.Init). Same DSP/UI core, a different hardware shell: 16 effects in a 4×4 grid
(four banks of four), global dry/wet mix, click-free patch changes.

This app was migrated here from the `eurorack_modules` repo, where it had been built on
the Arduino/DaisyDuino framework. DaisyDuino bundles a frozen, older DaisySP that lacks
`LadderFilter` and behaves differently around SDRAM static init; building on
libDaisy + current DaisySP (like the rest of this repo) removes that version skew.

## Banks

- **A REVERB** — Classic / Plate / Tank / Shimmer
- **B DELAY** — Ping / Tape / MultiTap / EchoVerb (tap-tempo on CV2)
- **C TONE** — Ladder / SVF morph / Comb / Dual LP
- **D MISC** — Resonator / Pitch / Drive / Crush

## Controls

The Seed exposes 3 pots + 2 CV (vs Patch.Init's 4 knobs), so the third effect param is
fixed per patch ("2-knob" mapping):

- **P1** = global dry/wet Mix (no CV)
- **P2** → effect p1 — **CV1** sums in as a bipolar offset
- **P3** → effect p2 — **CV2** sums in likewise, **except on the Delay bank**, where
  CV2 is the tap-tempo *clock* input only and p2 stays pot-only
- effect **p3** = fixed musical default per patch
- **Button** — short: next patch; long: Bank menu (2×2 grid on the 128×64 OLED)

### CV behaviour

CV jacks are **bipolar (±5 V)** and act as an *offset added to the pot*: **0 V = pot
alone**, **+5 V = +full range**, **−5 V = −full range**, clamped to 0..1. So a patched
CV always does something regardless of pot position, and the pot still reaches a true
zero. A small ±0.05 V dead zone ignores any DC sitting on an unpatched jack.

A marker above a bar means a CV is currently offsetting that parameter. (No marker on
the Delay bank's second bar — CV2 is the clock there, not a modulator.)

> Historical note: CV used to *replace* the pot once it was turned below ~0.015
> (hysteresis takeover). That meant a patched CV did nothing unless the pot was at the
> bottom, and the bottom of every range was unreachable from the pot. Replaced by
> summing.

### Per-patch CV mapping

**CV1 → p1** and **CV2 → p2** in every patch (except Delay, see above):

| Bank | Patch | p1 (P2/CV1) | p2 (P3/CV2) | p3 (fixed) |
| --- | --- | --- | --- | --- |
| RVB | CLASSIC | Decay | Tone | 0.50 (unused) |
| RVB | PLATE | PreDelay | Tone | 0.50 size |
| RVB | TANK | PreDelay | Tone | 0.60 size |
| RVB | SHIMMER | Decay | Tone | 0.50 shimmer |
| DLY | PING | Time | Fdbk *(pot only)* | 0.40 damp |
| DLY | TAPE | Time | Fdbk *(pot only)* | 0.35 wow |
| DLY | MULTITAP | Time | Spread *(pot only)* | 0.30 fb |
| DLY | ECHOVERB | Time | Fdbk *(pot only)* | 0.45 blend |
| TON | LADDER | Cutoff | Reso | 0.30 drive |
| TON | SVF | Cutoff | **Morph** (LP→BP→HP→Notch) | reso pinned 0.35 |
| TON | COMB | Freq | Fdbk | 0.40 damp |
| TON | **DUAL** | **Cutoff L** | **Cutoff R** | 0.30 shared reso |
| MSC | RESON | Freq | Damp | 0.70 level |
| MSC | PITCH | Pitch | Size | 0.50 fun |
| MSC | DRIVE | Drive | Tone | 0.80 level |
| MSC | CRUSH | Bits | Rate | 0.50 tone |

**SVF** is a morph filter, so P3/CV2 drives *morph* (not resonance) and resonance is
pinned — otherwise the patch could never morph.

**DUAL** is two *independent* mono ladder lowpass filters: IN L is filtered by
cutoff L, IN R by cutoff R, with a shared fixed resonance. Unlike every other patch it
is deliberately **not** stereo-linked — feed it two mono sources.
- Patch view shows three bars top-to-bottom matching the physical trimmers: **Mix**
  (top), then **P2**, then **P3**.
- **OLED hibernate** — after ~20 s idle the panel blanks and the display stops
  refreshing, so the I2C bus goes quiet (less audio-coupled noise, less OLED wear);
  any pot or button activity wakes it. The frame refresh runs in the main loop, never
  in the audio callback.

## Hardware

- **Daisy Seed** (STM32H750), homebrew Eurorack board.
- **128×64 SSD1306 OLED**, hardware **I2C1** — SCL `D11` (PB8), SDA `D12` (PB9), addr 0x3C.
- Pots/CV (single-ended ADC): P1 `D20`, P2 `D18`, P3 `D17`, CV1 `D16`, CV2 `D15`.
- Button `D1`, LED `D13`.

Pin names are libDaisy `daisy::seed::*`; they were translated 1:1 from the previous
Arduino build via the STM32 pin of each net.

## Build & flash

```sh
cd daisy_multifx_seed
make
make program-dfu      # DFU to internal flash (hold BOOT+RESET on the Seed)
```

## Notes

- **Output voicing**: DC-block + soft clamp only — the ~14.5 kHz LPF is disabled
  (`OutputStage::Init(sr, 0.f)`); the analog front end is voiced in hardware instead.
- **`g_tone` lives in SRAM**, not SDRAM: `LadderFilter`'s constructor writes to its
  members, which would fault if it ran (pre-`main`) before SDRAM is initialized. The
  other three banks are trivially constructible and stay in SDRAM. (Same as
  `daisy_multifx_oled`.)
- **SVF morph** is the fixed p3, so the Tone bank's `SVF` is a fixed-type (LP) filter
  here; the full morph sweep is available on the 4-knob Patch.Init build.
- Internal-flash image is ~88% of 128 KB; `-u _printf_float` is intentionally omitted
  (no float printf is used) to keep headroom.
