# daisy_multifx_seed

The shared [`multifx_core`](../common/multifx_core/) multi-FX on a bare **Daisy Seed**
with a home-built Eurorack front end — the sibling of [`daisy_multifx_oled`](../daisy_multifx_oled/)
(Patch.Init). Same DSP/UI core, a different hardware shell: 16 effects in a 4×4 grid
(four banks of four), global dry/wet mix, click-free patch changes.

This app was migrated here from the `eurorack_modules` repo, where it had been built on
the Arduino/DaisyDuino framework. DaisyDuino bundles a frozen, older DaisySP that lacks
`LadderFilter`/`Wavefolder` and behaves differently around SDRAM static init; building on
libDaisy + current DaisySP (like the rest of this repo) removes that version skew.

## Banks

- **A REVERB** — Classic / Plate / Tank / Shimmer
- **B DELAY** — Ping / Tape / MultiTap / EchoVerb (tap-tempo on CV2)
- **C TONE** — Ladder / SVF / Comb / Wavefolder+Chorus
- **D MISC** — Resonator / Pitch / Drive / Crush

## Controls

The Seed exposes 3 pots + 2 CV (vs Patch.Init's 4 knobs), so the third effect param is
fixed per patch ("2-knob" mapping):

- **P1** = global dry/wet Mix
- **P2** → effect p1 — **CV1** takes over (hysteresis)
- **P3** → effect p2 — **CV2** takes over, or arms tap-tempo on the Delay bank
- effect **p3** = fixed musical default per patch
- **Button** — short: next patch; long: Bank menu (2×2 grid on the 128×64 OLED)
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
