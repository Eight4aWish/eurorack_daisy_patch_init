# Universal Panel & Control Map

Reference for a single physical panel shared by every selectable firmware on the
hand-built Daisy Patch Init module. The panel labels are fixed; each firmware
conforms to the roles below, and the OLED prints the exact per-app meaning of
the variable controls.

This targets the "occasional-use" firmware family intended to be selectable at
boot: `daisy_fm4op` (FM), `daisy_interval_osc` (dual osc), `daisy_scanned`
(scanned synthesis). New synth-voice firmwares are expected to follow the same
contract.

> **Scope.** This document is the *design contract for that family*, not a
> description of everything flashed on the module.
>
> - The contract is **implemented** by [`daisy_multiosc/`](../daisy_multiosc/),
>   which hosts FM4OP, INTVL, SCAN, BYTEBEAT and a SINE test voice behind the boot-time
>   chooser described below.
> - **Joy** (`daisy_braids_oled/`) and **Joy Lite** (`daisy_joy_lite/`) are
>   separate firmwares on this same panel that **do not follow the contract**.
>   They predate it and spend all four knobs on voice parameters, so there is
>   no `TUNE` knob and no Play/Edit page. Their actual map is under
>   [Per-firmware maps](#per-firmware-maps).

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
by the OLED — for every firmware in this contract, and for Joy.

**Exception:** **Joy Lite** is screenless and uses B8 as its bank A/B toggle, so
the claim "B8 is unused in firmware" holds for everything *except* Joy Lite. The
toggle and the OLED compete for one panel position, so a given physical build
picks one. Joy Lite still runs on a Joy panel, but with B8 unpopulated the
internal pull-up reads as closed and it is stuck in **Bank B**.

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

What the Joy family makes of those same positions, since it is what is on the
module most of the time:

| Panel position | Contract meaning | Joy / Joy Lite |
| --- | --- | --- |
| `TUNE` knob | Coarse pitch | **Timbre** (no manual pitch control at all) |
| `MOD 1` knob | Screen-defined | **Color** |
| `MOD 2` / `MOD 3` knobs | Screen-defined | **Attack** / **Decay** |
| `V/OCT` CV | 1 V/oct | 1 V/oct (as contract) |
| `MOD 1` / `MOD 2` CV | Modulates matching knob | **Timbre CV** / **Color CV** (±50%) |
| `MOD 3` CV | Modulates matching knob | **FM** (~6 semitones/V, not 1 V/oct) |
| `TRIG` (Gate In 1) | Trigger | Trigger/gate — **unpatched = drone** |
| `GATE 2` (Gate In 2) | Reserved / per-app | **Hard sync** |
| `GATE OUT 1/2`, `CV OUT` | Per-app | Unused |
| `IN` (audio in) | Per-app | Unused |

If the panel is ever re-silked, `MOD 3` wants a `FM` sub-label and `GATE 2` a
`SYNC` one for the Joy family's sake.

### The pitch pair (fixed)

`TUNE` (manual coarse pitch) is **summed** with `V/OCT` (1 V/oct CV), in every
firmware that follows this contract. The tune knob is required even when V/Oct
is patched: it sets pitch when nothing is patched (standalone/drone/resonator
use), and provides transpose / fine-tune against an external sequencer. The
three firmwares listed above implement `pitch = TUNE_knob + V/OCT_cv`.

**Joy and Joy Lite are the exception**, and it is a real gap rather than an
oversight in this document: their pitch is `fixed base note + V/OCT + FM`, with
no manual pitch control, because all four knobs are spent on Timbre, Color,
Attack and Decay. Unpatched, they drone at a fixed C. A real Braids has COARSE
and FINE pots for this. Closing the gap needs either a knob freed up or a
settings page — see the Joy README's "Differences from Braids".

## Button (B7) gesture budget — universal

| Gesture | Meaning |
| --- | --- |
| Short press | Primary cycle (model / algorithm / waveform) — stays instant |
| Long press (~1 s) | Toggle **Play ↔ Edit** page (replaces the old B8 shift) |

Only two gestures. Deep/rare functions are not on a per-app gesture — they live
in a deep menu (a final Edit-page item) or are hard-coded with sane defaults.

There is no held "shift": the **Edit page is the shift layer**. On the Edit page
the three MOD knobs address each firmware's secondary parameters. Because a knob
changes meaning between pages, the Edit page uses **soft-takeover** (a knob does
not grab a value until swept through it) — the same mechanism already in
`daisy_braids_oled`. Play-page knobs stay live so performance feel is unaffected.

## Firmware selection — boot-time only

Implemented in [`daisy_multiosc/`](../daisy_multiosc/) (the Joy family is not
part of that image). The app chooser is not a runtime gesture, so it never
competes with B7:

- **At power-on:** OLED lists the firmwares. A MOD knob scrolls; short-press
  selects (auto-boots the last-used after a short timeout).
- **During an app:** B7 is entirely the app's.
- Optional later: a final Edit-page item `← Switch firmware` for runtime
  switching, rather than a dedicated gesture.

Implementation note: combined, these firmwares exceed the 128 KB internal flash,
so the unified image must build `BOOT_QSPI` (8 MB QSPI), like
`daisy_braids_oled`/`daisy_multifx_oled`. Switching apps re-initialises the audio
engine (sample rate / block size / buffers) per firmware so each keeps its native
DSP config; large buffers belong in SDRAM.

## Per-firmware maps

`TUNE` = pitch (+V/OCT) in all contract-following apps. `MOD n` CV jacks
modulate the matching MOD knob's parameter.

### Joy / Joy Lite (macro oscillator) — does not follow the contract

Documented here because it is what the module runs, not as a model to copy.

| Slot | Joy | Joy Lite |
| --- | --- | --- |
| TUNE | Timbre | Timbre |
| MOD 1 | Color | Color |
| MOD 2 | Attack (1 ms – 6 s) | Attack (1 ms – 6 s) |
| MOD 3 | Decay (1 ms – 6 s) | Decay (1 ms – 6 s) |
| Short press | Next patch (or next bank, in Bank mode) | Next model in bank |
| Long press | Toggle Patch ↔ Bank navigation | Re-blink model number |
| B8 | *(removed — OLED occupies the position)* | **Toggle: bank A / B** |

- No Play/Edit page and no soft-takeover: every knob is live at all times.
- `MOD 3` CV is **FM**, not a Decay modulator — it is Braids' FM input at full
  attenuverter depth (~6 semitones/V, deliberately not 1 V/oct).
- `GATE 2` is **hard sync**, and only the analog-family models respond to it;
  the digital, physical, percussion, wavetable and noise engines ignore sync,
  exactly as on a real Braids.
- Joy Lite keeps the B8 toggle, so it is the one firmware that still needs that
  switch fitted; on a Joy panel (toggle removed for the OLED) it runs but stays
  in Bank B.

### SCAN (scanned synthesis)

| Slot | Play page | Edit page |
| --- | --- | --- |
| TUNE | Pitch (scan rate) | — |
| MOD 1 | Tension | **Centering** |
| MOD 2 | Damping | — |
| MOD 3 | Hammer | — |
| Short press | cycle excitation shape (Pulse / Bump / Two / Noise) | — |

- `GATE` (Gate In 1) plucks/excites the spring-mass string. `OUT L / OUT R` =
  mono (same signal).
- Env **off** (default) = continuous self-excitation → evolving drone; env
  **on** = gated pluck that rings out per Damping.
- The scan rate (pitch) is decoupled from the spring dynamics, so a held note
  morphs organically. Spring constants are first-pass / tunable.

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

At 64 px and a 5×7 font, a line is **ten characters** — the hard budget every
name on screen has to live inside. The title is `NAME:SEL` where that fits
(`FM4OP:SwTr`); where it does not, the firmware name keeps the title line and
the selection drops to its own line under the knob grid, truncated to ten. An
engine whose Edit page has its own selection worth naming can return it from
`Engine::EditSelection()`, and that line reads `EDIT:<sel>` rather than `EDIT`. Keep
engine names short enough to leave room for a selection, and check any selection
string that reads as words rather than an abbreviation.
