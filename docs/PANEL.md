# Universal Panel & Control Map

Reference for the panel shared by every firmware on the hand-built Daisy Patch Init modules with an
OLED. The silk is fixed; each firmware gives the positions its own meaning, and the OLED prints it.

> **Scope.**
>
> - **The contract below is implemented** by [`daisy_multiosc/`](../daisy_multiosc/), which hosts
>   FM4OP, INTVL, SCAN and WAVES. It was redesigned with David on 2026-11-22 to work as his other
>   patch.Init apps do (Joy, Mirth, Wish, Sorrow): built the same day, **not yet flashed or played**.
> - **Joy** (`daisy_braids_oled/`) and **Joy Lite** (`daisy_joy_lite/`) are separate firmwares on
>   this panel. The contract follows their layout, attack and decay at the bottom, where it can:
>   their map is under [Joy and Joy Lite](#joy-and-joy-lite).

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

The four knobs sit two by two, knobs 1 and 2 on top, each CV jack below its knob. The former
**SW_2 ON-ON toggle (B8) is removed**: its panel position is taken by the OLED, for MultiOsc and for
Joy.

**Exception:** **Joy Lite** is screenless and uses B8 as its bank A/B toggle. The toggle and the OLED
compete for one panel position, so a given physical build picks one. Joy Lite still runs on a Joy
panel, but with B8 unpopulated the internal pull-up reads as closed and it is stuck in **Bank B**.

## The silk

```
 ( TUNE )    ( MOD 1 )   ( MOD 2 )   ( MOD 3 )      knobs   (CV_1..CV_4)
  V/OCT       MOD 1        MOD 2       MOD 3         CV in   (CV_5..CV_8)

  [ TRIG ]  [ GATE 2 ]       IN       OUT L  OUT R
   gate1     gate2         audio in    audio out

  GATE OUT 1   GATE OUT 2   CV OUT    ( • )    [ SEL ]
                                       LED      hold = menu
```

The silk predates both maps below: neither firmware has a TUNE knob, and the screen names every
knob. A printed face mask could relabel it, as Boy's do.

## The contract (MultiOsc)

- **No TUNE knob, and no transpose.** Pitch is V/OCT alone, through the module's calibration
  ([`../common/unit_cal.h`](../common/unit_cal.h)), with **C3 at 0 V** (MIDI 48), as on Joy.
- **Pot 1 picks, pots 2–4 shape.** Pot 1 sits over the V/OCT jack, so it is the one knob without
  CV: it holds each engine's selector. Pots 2–4 take the MOD 1–3 jacks below them, **±5 V moving
  the parameter ±half the knob's travel**, as Joy's Timbre and Color CV.
- **B7:**
  - **tap:** the engine's (INTVL's waveform pair; nothing in the others);
  - **hold and turn a knob:** a second bank of four, pot 1 **Volume** in every engine, pot 2 free,
    pot 3 **Attack**, pot 4 **Decay**, attack and decay on the bottom row as on Joy. The screen shows
    it from 0.3 s into a hold. Each knob **catches** its value before it moves it, and back on the
    main page the knobs catch theirs, so nothing jumps (as Wish);
  - **hold 2 s without turning:** the next engine, FM4OP → INTVL → SCAN → WAVES → FM4OP. No chooser:
    MultiOsc starts in the engine it was left in. 2 s is Wish's hold for the same kind of gesture;
  - **held at power-up:** the module's V/OCT calibration
    ([`daisy_multiosc/README.md`](../daisy_multiosc/README.md#calibration)).
- **The envelope follows TRIG**, Joy's own (`joy::AdEnvelope`, [`../common/joy_dsp.h`](../common/joy_dsp.h)):
  a drone until the first gate, then attack while the gate is high and decay after it, until the
  engine changes. Attack and decay run 1 ms to 6 s on Joy's squared law. No envelope switch.
- **Volume** is squared, so the knob feels even; every engine peaks near full scale at full volume,
  and starts at 0.8 of the knob.
- **Saved** to QSPI at 0x7C0000, 3 s after the last change: the engine, and each engine's tap state
  and second bank. MultiOsc runs `BOOT_SRAM` so it can write the flash.
- **The idle jacks stay idle:** GATE 2, both gate outs, CV OUT, audio IN and the LED (David).

| Engine | Pot 1 (no CV) | Pot 2 | Pot 3 | Pot 4 | Tap | Hold + pot 3 / 4 |
| --- | --- | --- | --- | --- | --- | --- |
| FM4OP | **Algorithm**, eight | Ratio | Index | Feedback | — | Attack / Decay |
| INTVL | **Mode**: Interval, Rational | Interval, or the ratio | Detune | Pulse width | Waveform pair | Attack / Decay |
| SCAN | **Excitation**: Pulse, Bump, Two, Noise | Tension | Hammer | Centring | — | inactive / Decay = Damping |
| WAVES | **Shape**: Sin, Tri, Saw, Sqr | Pulse width (Sqr) | — | — | — | Attack / Decay |

### FM4OP

Eight algorithms, from one carrier with a deep stack to four sines mixed. Operator 1 is always a
carrier, operator 4 carries the feedback, and every routing runs from a higher operator to a lower:

| Pot 1 | Routing | Carriers |
| --- | --- | --- |
| Alg1 | 4 → 3 → 2 → 1 | 1 |
| Alg2 | (3 + 4) → 2 → 1 | 1 |
| Alg3 | (4 → 3) + 2 → 1 | 1 |
| Alg4 | (2 + 3 + 4) → 1 | 1 |
| Alg5 | 4 → 3, 2 → 1 | 2 |
| Alg6 | 4 → 1, 2, 3 | 3 |
| Alg7 | 4 → 3, plus 2 and 1 | 3 |
| Alg8 | 1 + 2 + 3 + 4 | 4 |

Pots 2–4 mean the same in every algorithm:

- **Ratio** steps through eleven sets for operators 2, 3 and 4 against operator 1 at the note:
  1·1·1, 1·1·2, 2·2·2, 2·3·4, 0.5·1·2, 3·5·7, 2·4·8, 1·2·3, then inharmonic: 1.41·1·1 (root 2),
  3.5·1·1, and 2.76·5.40·8.93 (a struck bar's overtones). In Alg8 they act like drawbars. A starting
  table, to tune by ear.
- **Index**, how much modulation, every modulator together, up to 8 on a squared law. At zero the
  carriers are plain sines.
- **Feedback**, operator 4 modulating itself, up to 3 on a squared law: from a sine through saw-like
  to noisy.

The modulation stays **frequency-scaled**, as the original fm4op: what a modulator adds is scaled by
its target's own increment, so the timbre holds across the keyboard. A rewrite to raw phase
modulation with single-sample feedback aliased badly once and was reverted; the feedback here is
frequency-scaled too, and averages operator 4's last two samples. Carriers are mixed and divided by
their number. On the Mac (2026-11-22), every algorithm, ratio set, Index and Feedback at their ends
and notes from C1 to C8 stayed finite and inside full scale.

### INTVL (dual oscillator)

Nick Donaldson's IntervalOsc. Pot 1 picks **Interval** mode (pot 2 sets the second oscillator's
offset, −24 to +24 semitones) or **Rational** (pot 2 steps through just ratios, 1/4 to 16). Pot 3
detunes the two apart, up to a semitone each way; pot 4 is the pulse width. The tap steps through ten
waveform pairs (Sin, Tri, Saw, Sqr, and Saw or Sqr against another), shown in the title. Both
oscillators are summed to both outputs.

### SCAN (scanned synthesis)

A ring of 64 masses on springs, read round at the note's pitch, so the waveform is the ring's shape
and keeps moving. Pot 1 is how it is struck (**Pulse**, one mass; **Bump**, a smooth hump; **Two**, a
hump up and one down half a ring apart, odd harmonics only; **Noise**). **Tension** is how fast the
shape changes, **Hammer** how hard each strike, **Centring** the pull back to rest. Its decay is its
**Damping**, on hold + pot 4 (0.03 to 0.0005, exponential); a struck ring has no attack, so hold +
pot 3 does nothing, and the host's envelope isn't used. Until the first gate it is a drone, fed all
the time; from then on each gate strikes it. With no gates, Damping sets how the drone settles
against the feed.

### WAVES (plain waveforms)

One oscillator: pot 1 the shape, pot 2 the pulse width (square only; anticlockwise a square,
clockwise a 5 % pulse). Every shape peaks at the same level. Pots 3 and 4 are free.

## Joy and Joy Lite

| Knob | Joy | Joy Lite |
| --- | --- | --- |
| 1 | Timbre (+ CV_6, ±50 %) | Timbre (+ CV_6) |
| 2 | Color (+ CV_7, ±50 %) | Color (+ CV_7) |
| 3 | Attack (1 ms – 6 s) | Attack (1 ms – 6 s) |
| 4 | Decay (1 ms – 6 s) | Decay (1 ms – 6 s) |
| Short press | Next patch (or next bank, in Bank mode) | Next model in bank |
| Long press | Toggle Patch ↔ Bank navigation | Re-blink model number |
| B8 | *(removed — OLED occupies the position)* | **Toggle: bank A / B** |

- Every knob is live at all times.
- CV_8 is **FM**, Braids' FM input at full attenuverter depth (~6 semitones/V, deliberately not
  1 V/oct), less the module's own empty-jack reading measured at calibration.
- `GATE 2` is **hard sync**, and only the analog-family models respond to it, exactly as on a real
  Braids.
- Pitch is `C3 at 0 V + V/OCT + FM`: no manual pitch control, as MultiOsc now. Unpatched, they drone
  at a fixed C.

## Screen legend (MultiOsc)

The OLED shows the engine's name, then a 2×2 grid laid out as the knobs sit: pot 1's current choice
and pot 2's label on the top row, pots 3 and 4 below. While B7 is held it shows the second bank
instead (`Vol -` / `Atk Dec`, SCAN `- Damp`) and the title reads `NAME:EDIT`. Changing engine shows
its name large for a moment. The screen redraws only when something on it changes: the bit-banged
I2C couples into the codec as noise.

At 64 px and a 5×7 font, a line is **ten characters**, the hard budget every name on screen has to
live inside: five for pot 1's choice and a space, four for the label beside it. The title is
`NAME:SEL` where the tap selects something and that fits (`INTVL:SwTr`); where it does not, the
selection drops to its own line under the grid. [`../common/tools/panel_check.cpp`](../common/tools/)
(`make check`) renders every engine's title and every cell through the real driver and fails on
anything that clips; the cells it checks are copied from the engines, so keep them in step.
