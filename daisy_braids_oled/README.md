# Joy

*"Two for joy…"*

**Joy** is a macro oscillator for the Electrosmith Daisy Patch Init, with a 64x48 OLED
for patch navigation. It is the second in a family of firmwares named after the magpie
counting rhyme.

Based on the **Braids** macro oscillator by Émilie Gillet (Mutable Instruments), MIT.
*Not affiliated with, or endorsed by, Mutable Instruments or Electrosmith.*

> Prefer no screen? [**Joy Lite**](../daisy_joy_lite/) is the screenless sibling —
> a curated 16 models (the ones Plaits doesn't cover) on LED + button + toggle,
> sharing Joy's calibration and DSP.

## Overview

A macro oscillator with 48 models, a 64x48 OLED display for visual feedback and
two-level menu navigation. The control scheme uses all four knobs for sound parameters,
with patch selection handled via the B7 button. The last selected patch is remembered
across power cycles (saved to QSPI flash a couple of seconds after each change).

## Hardware Requirements

- **Daisy Patch.Init** (or Patch SM with breakout)
- **64x48 SSD1306 OLED** (0.66" I2C display, address 0x3C)
- **Expansion Header Connection:**
  - A2 (PORTA,1) → SDA
  - A3 (PORTA,0) → SCL
  - 3V3 → VCC
  - GND → GND

> **Note:** The B8 toggle switch must be removed to make physical panel space for the OLED screen.

## Controls

### Knobs

| Knob | Function | Range |
|------|----------|-------|
| **KNOB 1** (CV_1) | Timbre | Model-dependent — the display shows what it does on the current model |
| **KNOB 2** (CV_2) | Color | Model-dependent — the display shows what it does on the current model |
| **KNOB 3** (CV_3) | Attack | 1ms → 6 seconds (exponential) |
| **KNOB 4** (CV_4) | Decay | 1ms → 6 seconds (exponential) |

### Navigation (B7 Button)

The button provides two-level menu navigation:

- **Short Press:** Cycle within current mode (patches or banks)
- **Long Press (500ms):** Toggle between Patch mode and Bank mode

The display highlights the currently active navigation level:
- In **Patch mode**: The patch name is inverted/highlighted
- In **Bank mode**: The bank name is inverted/highlighted

### CV Inputs

| Input | Function |
|-------|----------|
| CV_5 | V/Oct pitch (C3 at 0V, ±5 octaves) |
| CV_6 | Timbre modulation (±50% depth) |
| CV_7 | Color modulation (±50% depth) |
| CV_8 | FM (±30 semitones at ±5V ≈ 6 semitones/V) |

**FM (CV_8)** is Braids' FM input, ported at the depth Braids gives it with the
MOD attenuverter fully clockwise — `adc_to_fm()` scales a full-scale swing to
±30 semitones. It is deliberately *not* 1 V/oct: it's a depth control, not a
second pitch input, so it takes the raw CV rather than the V/Oct calibration.
Braids has an attenuverter knob for this and Joy has no knob to spare, so the
depth is fixed — attenuate at the source for less. Like Braids, FM is applied
once per render block, so it is control-rate FM.

### Gate Inputs

| Input | Function |
|-------|----------|
| GATE IN 1 | Trigger/Gate for AD envelope — **unpatched = drone** |
| GATE IN 2 | Hard sync (reset oscillator phase) |

With nothing patched to GATE IN 1 the VCA stays open and the oscillator drones,
which is a stock Braids' default too (its AD→VCA depth ships at zero, so a
Braids sings continuously and TRIG only strikes the model). The first gate edge
hands the VCA to the AD envelope for the rest of the power cycle, so behaviour
with a gate patched is unchanged. Hard sync only affects the models that use it —
the analog and sync/triple families; the digital, physical, percussion,
wavetable and noise engines ignore it, exactly as on a real Braids.

## Sample rate

Audio runs at **96 kHz**. Braids' firmware clocks its render timer at
`F_CPU/96000` and its oscillator increment/delay lookup tables are generated for
that rate, so the vendored DSP only behaves as written at 96 kHz. Builds before
v1.4 ran at libDaisy's 48 kHz default, consuming those per-sample increments at
half speed: every model played an octave above its nominal pitch, every
table-derived time constant (comb delays, resonators, drum decays, grain rates)
ran twice as fast, and the band-limited tables were chosen for a Nyquist twice
as high as the one actually in use, so bright models aliased more than they
should.

> **Upgrading from v1.3 or earlier:** pitch drops an octave. Your saved V/Oct
> calibration is unaffected and carries over — the calibration is in the CV
> domain. If you would rather keep the pitch older builds produced, rebuild with
> `-DVOCT_BASE_MIDI=60`.

## Differences from Braids

The DSP is Mutable's own, compiled unmodified from the `eurorack` submodule —
`macro_oscillator.cc`, `analog_oscillator.cc`, `digital_oscillator.cc`,
`resources.cc`, all 48 shapes including the hidden `????`. What differs is
everything around it: Braids' `RenderBlock()` does a lot before and after
`osc.Render()`, and Joy is a different front end.

**Worth knowing first:** Braids ships those extras *switched off*
(`settings.cc` `kInitSettings` — 16-bit, 96 kHz, signature 0, drift off,
quantizer off, meta off, all AD routings 0). At factory defaults its bit mask is
`0xffff`, its decimation factor is 1 and its waveshaper mix is zero, so the
whole post-oscillator chain is a no-op. **Default against default, Joy's voice
matches a stock Braids.** The list below is missing *features*, not missing
fidelity.

### Not ported

| Braids | Status in Joy |
|---|---|
| `RESO` bit reduction (2–16 bit) | Absent |
| `RATE` sample-rate decimation (4k–96k) | Absent |
| `SIGN` signature waveshaper | Absent — `signature_waveshaper.h` isn't compiled in |
| `DRFT` VCO drift / `FLAT` VCO flatten | Absent — `vco_jitter_source.h` isn't compiled in |
| Quantizer: 49 scales + root | Absent — `quantizer.cc` isn't compiled in |
| `META` (CV → model selection) | Absent |
| Pitch range / octave, incl. the hidden LFO range | Absent — so no sub-audio use |
| COARSE and FINE tune pots | Absent — no manual pitch control at all (see below) |
| AD → TIMBRE / COLOR / FM routing | Absent — the envelope reaches the VCA only |
| `TRIG DELAY`, auto-trigger on pitch change | Absent |
| Per-input CV offset/scale calibration | V/Oct only (Joy calibrates it on-module; Timbre/Color CV are fixed ±50%) |
| Marquee, CV tester, brightness, `paques` easter egg | Absent — `????` is a normal patch here instead |

All of these are global settings on a real Braids, reached through its encoder
menu. Adding any of them to Joy needs a settings page, since all four knobs and
both B7 gestures are already spent.

**The one that is a real gap rather than a choice:** Braids has COARSE and FINE
pitch pots. Joy's pitch is `fixed base note + V/Oct + FM`, so there is no way to
tune or transpose from the panel, and unpatched it drones at a fixed C.

### Behaviour that differs

- **Envelope.** Braids' internal AD is exponential (`lut_env_expo`), reaches
  ~12 s, has no sustain segment, and is routable in variable amounts to four
  destinations. Joy's is a linear AD **with a sustain segment**, up to 6 s,
  hardwired to the VCA — but on two continuous knobs rather than Braids' 0–15
  menu settings, which is the trade Joy is making.
- **Trigger timing.** Braids samples TRIG once per sample in its timer ISR. Joy
  reads a rising edge once per 24-sample render block.
- **Sync.** Braids has no sync jack: it reuses TRIG, and `RenderBlock()` clears
  the sync buffer whenever any AD routing is on, so a real Braids cannot
  trigger and sync at the same time. Joy gives sync its own jack (GATE IN 2),
  block-quantised the same way the trigger is.
- **Output.** Mono, duplicated to both outputs through the Daisy's 24-bit codec,
  versus Braids' single output.

## Oscillator Banks

All 48 Braids oscillator shapes are organized into 8 thematic banks of 4-8 patches,
following the section groupings used by the Braids manual itself (analog, physical,
percussion, wavetables, noise, ...). Model names match what a real Braids shows on its
display (and what the manual's model table lists), so the manual can be followed
directly. Where Braids draws waveform glyphs, Joy uses the same ASCII forms the manual
prints (`/|` saw, `/\` triangle, `-_` square, `_|` comb). The Timbre/Color columns
below are from the Braids quickstart fold-out table.

### Bank 1: ANALOG (6)
Classic analog waveforms, including the sub-oscillator variants.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `CSAW` | Notch width | Notch polarity | CS-80 imperfect saw |
| 2 | `/\/\|-_-_` | Waveshape | Distortion/filter | Variable waveshape |
| 3 | `/\|/\|-_-_` | Pulse width | Saw to square | Classic sawtooth/square |
| 4 | `FOLD` | Wavefolder amount | Sine to triangle | Sine/triangle into wavefolder |
| 5 | `SUB-_` | Pulse width | Sub octave & level* | Square with sub-oscillator |
| 6 | `SUB/\|` | Saw shape | Sub octave & level* | Sawtooth with sub-oscillator |

*\* The SUB models postdate the printed fold-out; their Timbre/Color behaviour is taken
from the Braids source (`MacroOscillator::RenderSub`): COLOR below centre = sub 2 octaves
down, above centre = 1 octave down, distance from centre = sub level.*

### Bank 2: SYNC+3X (6)
Hardsync pairs and triple oscillators.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `SYN-_` | VCO frequency ratio | VCO balance | 2 VCOs with hardsync (square) |
| 2 | `SYN/\|` | VCO frequency ratio | VCO balance | 2 VCOs with hardsync (saw) |
| 3 | `/\|/\| x3` | Osc. 2 detune | Osc. 3 detune | Triple saw |
| 4 | `-_ x3` | Osc. 2 detune | Osc. 3 detune | Triple square |
| 5 | `/\ x3` | Osc. 2 detune | Osc. 3 detune | Triple triangle |
| 6 | `SI x3` | Osc. 2 detune | Osc. 3 detune | Triple sine |

### Bank 3: STACK+FM (8)
Spectral builders: combs, ring mod, swarm, additive and FM.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `_\|_\|_\|_` | Smoothness | Detune | 2 detuned harmonic combs |
| 2 | `RING` | 2/1 frequency ratio | 3/1 frequency ratio | 3 ring-modulated sine waves |
| 3 | `/\|/\|/\|/\|` | Detune | High-pass filter | Swarm of 7 sawtooth waves |
| 4 | `/\|/\|_\|_\|` | Delay time | Neg./pos. feedback | Comb filtered sawtooth |
| 5 | `HARM` | Harmonic # | Spectral peakedness | Additive synth, 14 harmonics |
| 6 | `FM` | Modulation index | Frequency ratio | Plain 2-operator FM |
| 7 | `FBFM` | Modulation index | Frequency ratio | Feedback 2-operator FM |
| 8 | `WTFM` | Modulation index | Frequency ratio | Chaotic 2-operator FM |

### Bank 4: FLT+VOX (7)
Digital filters and vocal/formant synthesis.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `ZLPF` | Cutoff frequency | Waveshape | Direct synthesis, LP filtered |
| 2 | `ZPKF` | Cutoff frequency | Waveshape | Direct synthesis, peaking filtered |
| 3 | `ZBPF` | Cutoff frequency | Waveshape | Direct synthesis, BP filtered |
| 4 | `ZHPF` | Cutoff frequency | Waveshape | Direct synthesis, HP filtered |
| 5 | `VOSM` | Formant 1 frequency | Formant 2 frequency | Sawtooth with 2 formants |
| 6 | `VOWL` | a, e, i, o, u | Gender | Low-fi vowel synthesis |
| 7 | `VFOF` | a, e, i, o, u | Gender | Hi-fi vowel synthesis (FOF) |

### Bank 5: PHYSIC (4)
The manual's "Physical simulations" section, exactly.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `PLUK` | Decay | Plucking position | Plucked strings |
| 2 | `BOWD` | Friction | Bowing position | Bowed string |
| 3 | `BLOW` | Air pressure | Instrument geometry | Reed simulation |
| 4 | `FLUT` | Air pressure | Instrument geometry | Flute simulation |

### Bank 6: PERCUS (6)
The manual's "Percussions" section, plus the hidden extra.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `BELL` | Decay | Harmonicity | Bell |
| 2 | `DRUM` | Decay | Harmonicity | Metallic drum |
| 3 | `KICK` | Decay | Brightness | 808 bass drum |
| 4 | `CYMB` | Cutoff | Noisiness | Cymbal noise |
| 5 | `SNAR` | Tone | Noisiness/decay | 808 snare drum |
| 6 | `????` | ? | ? | Hidden extra — not in Braids' own menu |

### Bank 7: WAVES (4)
The manual's "Wavetables" section, exactly.

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `WTBL` | Wavetable position (smooth) | Wavetable selection (quantized) | 21 wavetables |
| 2 | `WMAP` | X position | Y position | 16x16 waves |
| 3 | `WLIN` | Wavetable position | Interpolation quality | Linear wavetable scanning |
| 4 | `WTx4` | Wavetable position | Chord type | Polyphonic wavetable |

### Bank 8: NOISE (7)
The manual's "Noise" section, plus TOY* (lo-fi/glitch).

| # | Model | Timbre | Color | Description |
|---|-------|--------|-------|-------------|
| 1 | `NOIS` | Filter resonance | Response, LP to HP | Tuned noise (2-pole filter) |
| 2 | `TWNQ` | Resonance | Resonators freq. ratio | Noise sent to 2 resonators |
| 3 | `CLKN` | Cycle length | Quantization | Clocked digital noise |
| 4 | `CLOU` | Grain density | Frequency dispersion | Sinusoidal granular synthesis |
| 5 | `PRTC` | Grain density | Frequency dispersion | Droplets granular synthesis |
| 6 | `QPSK` | Bit-rate | Modulated data | Modem noises |
| 7 | `TOY*` | Sample reduction | Bit toggling | Low-fi circuit-bent sounds |

## Display Layout

In **Patch mode** the 64×48 pixel OLED shows:

```
┌────────────┐
│   ANALOG   │  ← Bank name
│    CSAW    │  ← Model name, as on a real Braids
│────────────│
│ WIDT  POLR │  ← What TIMBRE (knob 1) / COLOR (knob 2) do on this model
│ ATK   DCY  │  ← Internal AD envelope (knobs 3 / 4)
└────────────┘
```

In **Bank mode** (B7 long press) the display shows a 3×3 grid of bank names with the
current bank highlighted.

## Building

### What you need first

- **The ARM toolchain** — `arm-none-eabi-gcc` and friends, plus `make`. The easiest way
  to get these is Electrosmith's [Daisy Toolchain](https://github.com/electro-smith/DaisyToolchain)
  (their docs walk through it for macOS, Windows and Linux).
- **`dfu-util`** — only needed if you flash over USB (see below). On macOS: `brew install dfu-util`.
- **The source, including its submodules.** This project builds against Electrosmith's
  libDaisy/DaisySP and Mutable's Braids code, which live in git submodules. If you cloned
  this repo without them, fetch them once from the repo root:

  ```bash
  git submodule update --init --recursive
  ```

### Build it

From this project's folder:

```bash
cd daisy_braids_oled
make
```

The first build also compiles the libDaisy/DaisySP libraries, so it takes a while. When it
finishes you'll have the firmware image at **`build/joy.bin`** — that's the file you flash to
the module in the next step.

> **Can't see `build/joy.bin` in your editor?** It's there on disk, but `build/` is
> git-ignored, so editors set to hide ignored files won't show it. Check on the command
> line with `ls build/joy.bin`.

## Flashing

Joy doesn't run straight off the chip like a plain Daisy program — it lives in the module's
large external flash and is launched by the **Daisy bootloader**, a tiny loader that has to
be installed on the module **once**. After that, updating Joy is just a matter of handing the
bootloader a new `joy.bin`.

Once the module is built into its panel, **the SD card is the only way to load or update
Joy** — the Daisy's USB port and its BOOT/RESET buttons are no longer reachable. So:

1. **Install the bootloader** — a one-time bench step done on the Daisy over USB, *before* it
   goes into the panel. See [First-time setup](#first-time-setup-installing-the-bootloader).
2. **Load Joy** from an SD card — the everyday way to install or update the firmware. See
   [Loading Joy from an SD card](#loading-joy-from-an-sd-card).

### First-time setup: installing the bootloader

Do this once, on the **bare Daisy while you still have USB access** (i.e. before it's mounted
in the module — afterwards the USB port and buttons are covered). It needs `dfu-util`
installed (see above).

1. Put the Daisy into its built-in "ST" boot mode: **hold the BOOT button, tap RESET, then
   release BOOT.**
2. From this folder, run:

   ```bash
   make program-boot
   ```

The bootloader is now installed permanently. You won't repeat this unless you deliberately
erase it.

### Loading Joy from an SD card

This is how you install Joy and how you'll update it later — no cables or extra software.

1. Format a microSD card as **FAT32** (also called MS-DOS FAT). Most small cards already are.
2. Copy **`build/joy.bin`** to the **top level** of the card. It must be the **only** `.bin`
   file on the card — delete any others.
3. **macOS only:** macOS hides a companion file named `._joy.bin` next to yours, and because
   it also ends in `.bin` it can confuse the bootloader. Remove these hidden files with
   `dot_clean` (replace `DAISY` with your card's name as it appears under `/Volumes`):

   ```bash
   dot_clean /Volumes/DAISY
   ```
4. Eject the card, put it in the module, and power-cycle. Shortly after power-up the
   bootloader checks the card, and if `joy.bin` differs from what's already installed it
   loads the new version, then starts Joy. Done.

> **Note for the curious.** Joy is a `BOOT_SRAM` build: the bootloader stores the binary
> in the module's external QSPI flash, copies it into SRAM at each power-up (a read, not
> a flash write) and runs it from there. Executing from SRAM leaves QSPI free to be
> written at runtime, which is how Joy remembers your last patch. The ST-Link/OpenOCD
> `make program` route used for ordinary Daisy programs is intentionally disabled here.

## Calibration

**On-module (no recompile).** Hold **B7 while powering up** to enter V/Oct
calibration. The OLED prompts a two-point routine:

1. `CALIBRATE 1V` — patch a **1 V** reference (C1) to V/Oct, press **B7**.
2. `CALIBRATE 3V` — patch **3 V** (C3, two octaves up), press **B7**.
3. `DONE` — the calibration is solved, **saved to flash**, and used immediately.

It persists across power cycles (stored alongside the last-patch setting). A
bad capture (e.g. nothing patched) is rejected and the previous calibration is
kept, so the routine can't leave the module mistracking. If you never calibrate,
the firmware uses sensible measured defaults.

**Compile-time defaults / overrides** (used as the fallback and for hard-coded
builds) can be set before `#include`:

```cpp
#define VOCT_BASE_MIDI   48        // MIDI note at 0V (default: C3)
#define VOCT_CENTER_NORM 0.007074f // ADC value at 0V (intercept)
#define VOCT_SLOPE       60.73f    // semitones per ADC unit (slope)
```

## License

**Joy** is MIT-licensed. It is based on Mutable Instruments Braids by Émilie Gillet,
also released under the MIT License — the original copyright notices are retained in
the vendored sources. See the original [Mutable Instruments repository](https://github.com/pichenettes/eurorack) for details.

"Braids" and "Mutable Instruments" are marks of their owner and are used here only to
describe this firmware's origin; "Daisy" is a mark of Electrosmith. Joy is an
independent community work, not affiliated with or endorsed by either.
