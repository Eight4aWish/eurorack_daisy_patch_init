# daisy_multifx_oled

Multi-effect processor for Daisy Patch.Init with a 64x48 OLED display. Built on the
shared, hardware-agnostic [`multifx_core`](../common/multifx_core/) DSP/UI library:
16 effects in a 4×4 grid (four banks of four), with a global dry/wet mix, output
voicing, and click-free patch changes.

## Features

- **4 banks × 4 patches** with a two-level Bank → Patch menu
- **Bank A REVERB** — Classic / Plate / Tank / Shimmer (voiced `ReverbSc`)
- **Bank B DELAY** — Ping / Tape / MultiTap / EchoVerb (low-passed feedback, smoothed times)
- **Bank C TONE** — Ladder / SVF morph / Comb / Dual LP (two independent mono ladders)
- **Bank D MISC** — Resonator / Pitch shifter / Drive / Crush
- **CV4 = global dry/wet mix** on every patch
- **CV-modulatable knobs** — each pot is summed with a CV input jack
- **Output voicing** (DC-block → ~14.5 kHz LPF → soft clamp) for a warmer, clip-safe output
- **Click-free patch changes** (wet fades in on switch; effect state is reset)
- **External clock sync** for delay time (Bank B: Ping / EchoVerb)

## Hardware Requirements

- **Daisy Patch.Init** (or Patch SM with breakout)
- **64x48 SSD1306 OLED** (0.66" I2C display, address 0x3C)
- **Expansion Header Connection:**
  - A2 (PORTA,1) → SDA
  - A3 (PORTA,0) → SCL
  - 3V3 → VCC
  - GND → GND

## Controls

### Navigation (B7 Button)

Two-level navigation (`mfx::NavModel`):

- **Patch view** — short press: next patch in the current bank; long press (500 ms): open the bank menu.
- **Bank menu** — short press: cycle the highlighted bank; long press: confirm it (jumps to patch 0).

A patch or bank change resets the effect and fades the wet signal back in (no pop).

### Knobs and CV

Each knob value is the corresponding pot **summed** with a CV input jack (Eurorack
attenuverter-style), clamped to 0–1:

| Knob | Pot | CV input | Role |
|------|-----|----------|------|
| CV_1 | Knob 1 | CV_5 | Param 1 |
| CV_2 | Knob 2 | CV_6 | Param 2 |
| CV_3 | Knob 3 | CV_7 | Param 3 (all patches except Reverb · Classic) |
| CV_4 | Knob 4 | CV_8 | **Global dry/wet mix** |

## Banks & Patches

CV_4 is the global dry/wet mix on every patch. CV_1–CV_3 are the three effect
params; Reverb · Classic is the only patch that leaves CV_3 unused.

### Bank A — REVERB
| Patch | CV_1 | CV_2 | CV_3 |
|-------|------|------|------|
| CLASSIC | Fbk – feedback/decay | Tone – LP frequency | — (unused) |
| PLATE | Pre – predelay | Tone – LP frequency | Size – reverb feedback |
| TANK | Pre – predelay | Damp – damping | Size – reverb feedback |
| SHIMMER | Fbk – feedback | Tone – LP frequency | Shim – shimmer amount (+12) |

### Bank B — DELAY
| Patch | CV_1 | CV_2 | CV_3 |
|-------|------|------|------|
| PING | Time\* | Fbk – feedback | Damp – feedback tone |
| TAPE | Time | Fbk – feedback | Wow – wow/flutter depth |
| MULTITAP | Time | Pan – tap spread | Fbk – feedback |
| ECHOVERB | Time\* | Fbk – feedback | RvMx – reverb blend |

\* PING and ECHOVERB lock their delay time to an external clock on **B10** while a
pulse has arrived within the last 2 s; otherwise CV_1 sets the time.

### Bank C — TONE
| Patch | CV_1 | CV_2 | CV_3 |
|-------|------|------|------|
| LADDER | Cut – cutoff | Res – resonance | Drv – input drive |
| SVF MRF | Cut – cutoff | Res – resonance | Typ – LP→BP→HP→Notch |
| COMB | Frq – pitch | Fbk – feedback | Brt – brightness |
| DUAL LP | CutL – cutoff, IN L | CutR – cutoff, IN R | Res – shared resonance |

**DUAL LP** is two *independent* mono ladder lowpass filters — IN L filtered by CutL,
IN R by CutR, sharing one resonance. Unlike every other patch it is deliberately **not**
stereo-linked; feed it two mono sources. Input drive is pinned (CV_3 is resonance).

### Bank D — MISC
| Patch | CV_1 | CV_2 | CV_3 |
|-------|------|------|------|
| RESONATR | Freq – base pitch | Damp – damping/bright | InLv – input level |
| PITCH | Semi – shift (−12..+12) | Size – buffer size | Fun – tape flutter |
| DRIVE | Drv – drive amount | Tone – post low-pass | Lvl – output level |
| CRUSH | Bits – bit depth | Rate – sample-rate reduce | Tone – post low-pass |

## CV/Gate & Audio

| Jack | Function |
|------|----------|
| **B10** (Gate In 1) | External clock input (Bank B delay sync) |
| **CV_OUT_2** | LED driver (blinks on patch/bank change) |
| **Audio In L/R** | Stereo input |
| **Audio Out L/R** | Stereo output (processed) |

## Display Layout

### Patch view (default)

```
┌────────────────┐
│    CLASSIC     │  ← patch name (centered)
│────────────────│
│  Fbk  Tone     │  ← CV_1, CV_2 labels
│       Mix      │  ← CV_3 (blank here), CV_4
└────────────────┘
```

### Bank menu (long-press B7)

```
┌───────┬────────┐
│  RVB  │  DLY   │
├───────┼────────┤
│  TON  │  MSC   │
└───────┴────────┘
```

The previewed bank is highlighted (inverted). Short-press cycles the preview;
long-press confirms it.

## Signal path (shared core)

Every patch computes a fully-wet stereo signal, then the shared
[`multifx_core`](../common/multifx_core/) stage handles the rest uniformly:

1. **Global mix** — CV_4 crossfades dry ↔ wet.
2. **Output voicing** — DC-block → soft clamp at ±1.2 (`OutputStage`), which protects the hot patches (Ladder, Comb, Dual) from clipping. The Seed's ~14.5 kHz LPF is **disabled** here (the Patch SM output is clean), so the full top end is preserved.
3. **Patch-change crossfade** — a patch/bank change fades the whole output out (~5 ms), runs the buffer-clearing bank `Reset()` while fully muted (off the audio interrupt), then fades back in (~30 ms). This avoids both cutting the previous reverb tail and any audible glitch from the SDRAM buffer clears (the pitch shifter alone zeros ~128 kB).

## Architecture

The DSP and navigation live in [`common/multifx_core/`](../common/multifx_core/) so
they can be shared with other Daisy boards; this project is a thin shell that wires
the core to the Patch SM hardware and the OLED. The banks (`ReverbBank`, `DelayBank`,
`ToneBank`, `MiscBank`) own `ReverbSc`/`DelayLine`/`PitchShifter` buffers and are
placed in SDRAM via `DSY_SDRAM_BSS`.

## Building

```bash
cd daisy_multifx_oled
make clean && make
```

## Flashing

Put Patch.Init in DFU mode (hold BOOT while pressing RESET), then:

```bash
make program-dfu
```
