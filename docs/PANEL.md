# Universal Panel & Control Map

Reference for a single physical panel shared by every selectable firmware on the
hand-built Daisy Patch Init module. The panel labels are fixed; each firmware
conforms to the roles below, and the OLED prints the exact per-app meaning of
the variable controls.

This targets the "occasional-use" firmware family selectable at boot:
`daisy_torus` (Rings), `daisy_fm4op`, `daisy_interval_osc`. New synth-voice
firmwares are expected to follow the same contract.

## Hardware surface (from `patch_init_schematic.pdf`)

| Control | Wiring | ADC / pin |
| --- | --- | --- |
| Knob 1–4 | KNOB1–4 (pots, 0–5 V) | `CV_1`–`CV_4` (ADC 0–3) |
| CV jack 1–4 | J_CV1–4 (normalled, ±5 V) | `CV_5`–`CV_8` (ADC 4–7) |
| Gate In 1 / 2 | J_GATEIN1/2 | B10 / B9 |
| Gate Out 1 / 2 | J_GATEOUT1/2 | B5 / B6 |
| CV Out 1 | J_CVOUT1 | C10 (`CV_OUT_1`) |
| CV Out 2 | panel LED (red) | C1 (`CV_OUT_2`) |
| Audio In L / R | J_LIN / J_RIN | B4 / B3 |
| Audio Out L / R | J_LOUT / J_ROUT | B2 / B1 |
| Button | SW_1 minibutton (momentary) | **B7** |
| OLED | SSD1306 64×48, soft I2C | **A2 = SDA, A3 = SCL** |

The former **SW_2 ON-ON toggle (B8) is removed** — its panel position is taken
by the OLED. B8 is unused in firmware.

## Label scheme (silk-ready)

Mixed convention: **fixed semantic names** for things that never change across
firmwares (and are unlikely to for future ones); **numbered `MOD n`** for things
that do, with each variable CV jack sitting directly below its matching pot. The
screen always shows what `MOD 1–3` currently mean.

```
 ( TUNE )    ( MOD 1 )   ( MOD 2 )   ( MOD 3 )      knobs   (CV_1..CV_4)
  V/OCT       MOD 1        MOD 2       MOD 3         CV in   (CV_5..CV_8)

  [ TRIG ]  [ GATE 2 ]       IN       OUT L  OUT R
   gate1     gate2         audio in    audio out

  GATE OUT 1   GATE OUT 2   CV OUT    ( • )    [ SEL ]
                                       LED      hold = menu
```

- **Fixed (named):** `TUNE`, `V/OCT`, `TRIG`, `IN`, `OUT L`, `OUT R`, `SEL`.
- **Variable (numbered, screen-defined):** `MOD 1/2/3` (+ matching `MOD n` CV),
  `GATE 2`, `GATE OUT 1/2`, `CV OUT`.
- The red LED (CV Out 2) is an activity/indicator output, not jacked.

### The pitch pair (fixed)

`TUNE` (manual coarse pitch) is **summed** with `V/OCT` (1 V/oct CV), in every
firmware. The tune knob is required even when V/Oct is patched: it sets pitch
when nothing is patched (standalone/drone/resonator use), and provides
transpose / fine-tune against an external sequencer. All three firmwares already
implement `pitch = TUNE_knob + V/OCT_cv`.

## Button (B7) gesture budget — universal

| Gesture | Meaning |
| --- | --- |
| Short press | Primary cycle (model / algorithm / waveform) — stays instant |
| Long press (~1 s) | Toggle **Play ↔ Edit** page (replaces the old B8 shift) |

Only two gestures. Deep/rare functions (e.g. Torus 1 V/oct calibration) are not
on a per-app gesture — they live in a deep menu (a final Edit-page item) or are
hard-coded with sane defaults until the calibration story is worked out.

There is no held "shift": the **Edit page is the shift layer**. On the Edit page
the three MOD knobs address each firmware's secondary parameters. Because a knob
changes meaning between pages, the Edit page uses **soft-takeover** (a knob does
not grab a value until swept through it) — the same mechanism already in
`daisy_braids_oled`. Play-page knobs stay live so performance feel is unaffected.

## Firmware selection — boot-time only

The app chooser is not a runtime gesture, so it never competes with B7:

- **At power-on:** OLED lists the firmwares. A MOD knob scrolls; short-press
  selects (auto-boots the last-used after a short timeout).
- **During an app:** B7 is entirely the app's.
- Optional later: a final Edit-page item `← Switch firmware` for runtime
  switching, rather than a dedicated gesture.

Implementation note: combined, these firmwares exceed the 128 KB internal flash,
so the unified image must build `BOOT_QSPI` (8 MB QSPI), like
`daisy_braids_oled`/`daisy_multifx_oled`. Switching apps re-initialises the audio
engine (sample rate / block size / buffers) per firmware so each keeps its native
DSP config; large buffers (e.g. Torus reverb) belong in SDRAM.

## Per-firmware maps

`TUNE` = pitch (+V/OCT) in all apps. `MOD n` CV jacks modulate the matching MOD
knob's parameter.

### Torus (Mutable Instruments Rings port)

| Slot | Play page | Edit page |
| --- | --- | --- |
| TUNE | Frequency | — |
| MOD 1 | Structure | **Damping** |
| MOD 2 | Brightness | **Polyphony** (1 / 2 / 4) |
| MOD 3 | Position | — (free) |
| Short press | cycle Resonator Model | — |

- `TRIG` (Gate In 1) = strum. `IN` (Audio In L) = exciter. `OUT L / OUT R` =
  main / aux voicings.
- Strum/note/exciter normalisation is auto-detected from patched jacks (no UI).
- Frequency promoted to TUNE; Damping moved to Edit (sits at a sweet spot most
  of the time).
- The Rings FM / string-synth "easter egg" is **not** a hidden mode here — it
  ships as its own firmware entry (e.g. `Torus String`) in the boot menu.
- 1 V/oct calibration: hard-coded defaults for now; deep menu later.

### fm4op (4-operator FM)

| Slot | Play page | Edit page |
| --- | --- | --- |
| TUNE | Pitch | — |
| MOD 1 | FM param A* | **Attack** |
| MOD 2 | FM param B* | **Release** |
| MOD 3 | FM param C* | **Volume** |
| Short press | cycle Algorithm (Parallel / Serial / Feedback) | — |

\*MOD 1–3 remap per algorithm (mod indices / ratios / feedback depth); the screen
labels them for the active algorithm. `TRIG` (Gate In 1) = envelope trigger.
`GATE 2` reserved for hard sync. Edit map matches existing firmware behaviour.

### interval_osc (dual oscillator)

| Slot | Play page | Edit page |
| --- | --- | --- |
| TUNE | Base freq | — |
| MOD 1 | Interval (quantized offset) | **Mode** (Interval / Rational) |
| MOD 2 | Detune | — (free) |
| MOD 3 | Pulse width | — (free) |
| Short press | cycle Waveform | — |

- `MOD 1` CV (CV_6) = offset CV; `MOD 3` CV (CV_8) = PWM CV (already wired).
- `OUT L / OUT R` = osc 1 / osc 2. Free Edit slots show "—" on screen.

## Screen legend (every app)

The OLED always shows: firmware name, current page (Play / Edit), the live
meaning of `MOD 1–3`, and the current short-press selection (model / algorithm /
waveform). This is the per-firmware control reminder the panel intentionally
omits.
