# Joy Lite

A **screenless** macro oscillator for the Electrosmith Daisy Patch Init — the
no-OLED sibling of [Joy](../daisy_braids_oled/). Where Joy gives you all 48 Braids
models with an OLED navigator, Joy Lite is a curated **16 models that Plaits
doesn't really cover**, navigated by the panel's LED + button + toggle.

Based on **Mutable Instruments Braids** by Émilie Gillet (MIT). *Not affiliated
with, or endorsed by, Mutable Instruments or Electrosmith.*

## Why these 16

Anyone running a Daisy Patch Init likely already has Plaits, so Joy Lite skips
everything Plaits does (VA, FM, additive, wavetables, speech, noise, physical,
drums) and ships the distinctively-Braids sounds instead. Two banks of 8,
selected by the **toggle**:

**Bank A (toggle down)** — CSAW · Saw-Sync · Ring-Mod · VOSIM · Digi-Filter BP ·
Chaotic-FM · QPSK · TOY

**Bank B (toggle up)** — Square-Sync · Buzz · Saw-Comb · Digi-Filter LP ·
Digi-Filter HP · Clocked-Noise · Twin-Peaks · ????

## Controls

| Control | Function |
| --- | --- |
| Knob 1 (CV_1) | Timbre |
| Knob 2 (CV_2) | Color |
| Knob 3 (CV_3) | Attack (1 ms – 6 s) |
| Knob 4 (CV_4) | Decay (1 ms – 6 s) |
| CV_5 | V/Oct pitch |
| CV_6 / CV_7 | Timbre / Color modulation (±50%) |
| CV_8 | FM — Braids' FM input at full attenuverter (≈6 semitones/V, not 1 V/oct) |
| Gate In 1 | Trigger / gate (envelope) — **unpatched = drone** |
| Gate In 2 | Hard sync |
| **Toggle (B8)** | Bank A / Bank B (each bank remembers its last model) |
| **B7 short** | Next model in bank (wraps 1→8) — the **LED blinks the number** |
| **B7 long** | Re-blink the current model number |

The LED blinks the model number (1–8) whenever you change model or flip the
bank, and is otherwise dark (it deliberately does **not** flash on every gate).
The last model per bank is saved to flash.

With **nothing patched to Gate In 1** the VCA stays open and the oscillator
drones — the same default a stock Braids has (its AD→VCA depth ships at zero).
The first gate edge hands the VCA to the AD envelope for the rest of the power
cycle, so patched behaviour is unchanged. If you want the envelope with a
stopped sequencer patched, just send one gate.

## Calibration

Hold **B7 at power-up** to calibrate V/Oct (no screen needed):

1. **LED solid** — patch **1 V** (C1) to V/Oct, press **B7**.
2. **LED blinking** — patch **3 V** (C3), press **B7**.
3. **Rapid flurry** — done; saved to flash and used immediately.

A bad capture is rejected (keeps the previous calibration). If never calibrated,
sensible measured defaults are used. Compile-time overrides: `VOCT_CENTER_NORM`,
`VOCT_SLOPE`, `VOCT_BASE_MIDI`.

## Build & flash

```sh
make                  # build/joy_lite.bin (APP_TYPE=BOOT_SRAM)
```

Runs via the Daisy bootloader (same as Joy): copy `joy_lite.bin` to a FAT32 SD
card root (only `.bin` there; on macOS `dot_clean` the card after), power-cycle;
or `make program-dfu` during the bootloader grace period. If the module has no
bootloader yet, install it once with `make program-boot`.

## Sample rate

Audio runs at **96 kHz**, the rate Braids itself runs at and the rate its
oscillator increment/delay tables are generated for. Builds before this ran at
libDaisy's 48 kHz default, which played every model an octave sharp and ran
every table-derived time constant (comb delays, resonators, drum decays, grain
rates) at double speed. **Pitch therefore drops an octave versus older builds** —
recalibration is not needed, the V/Oct calibration is unaffected.

## Differences from Braids

Same as Joy — see [that README's "Differences from Braids"](../daisy_braids_oled/README.md#differences-from-braids)
for the full list; the DSP and the front-end trade-offs are identical. The short
version: the Braids DSP is unmodified, and the settings Braids layers around it
(RESO, RATE, SIGN, drift, quantizer, META, AD routing, COARSE/FINE tune) are not
ported — but Braids ships all of them off, so default against default the voice
matches. Joy Lite additionally carries only 16 of the 48 models.

**Panel note:** Joy Lite needs the B8 toggle fitted. On a panel built for Joy
(toggle removed to make room for the OLED) it still runs, but the internal
pull-up reads as closed and it stays in Bank B.

## Shared code

Reuses [`../common/voct_cal.h`](../common/voct_cal.h) (calibration math) and
[`../common/joy_dsp.h`](../common/joy_dsp.h) (envelope + fixed-point helpers)
with Joy, plus the Braids DSP from the `eurorack` submodule. Only the front-end
(screenless nav vs OLED) differs.

## Licence

MIT (this firmware) over Mutable Instruments Braids (MIT). "Braids" and "Mutable
Instruments" are marks of their owner; "Daisy" of Electrosmith — used here only
to describe origin.
