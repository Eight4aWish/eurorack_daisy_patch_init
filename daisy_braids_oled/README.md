# Joy

*"Two for joy…"*

**Joy** is a macro oscillator for the Electrosmith Daisy Patch Init, with a 64x48 OLED
for patch navigation. It is the second in a family of firmwares named after the magpie
counting rhyme.

Based on the **Braids** macro oscillator by Émilie Gillet (Mutable Instruments), MIT.
*Not affiliated with, or endorsed by, Mutable Instruments or Electrosmith.*

## Overview

A macro oscillator with 48 models, a 64x48 OLED display for visual feedback and
two-level menu navigation. The control scheme uses all four knobs for sound parameters,
with patch selection handled via the B7 button.

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
| **KNOB 1** (CV_1) | Timbre | Shape-dependent primary parameter |
| **KNOB 2** (CV_2) | Color | Shape-dependent secondary parameter |
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

### Gate Inputs

| Input | Function |
|-------|----------|
| GATE IN 1 | Trigger/Gate for AD envelope |
| GATE IN 2 | Hard sync (reset oscillator phase) |

## Oscillator Banks

All 48 Braids oscillator shapes are organized into 8 thematic banks of 6 patches each:

### Bank 1: ANALOG
Classic analog synthesizer waveforms and morphing.

| # | Patch | Description |
|---|-------|-------------|
| 1 | CSAW | Continuously variable saw/pulse |
| 2 | MORPH | Morphing between saw, square, triangle, sine |
| 3 | SQ/SW | Square/sawtooth blend |
| 4 | SI/TR | Sine/triangle blend |
| 5 | BUZZ | Sawtooth with variable harmonic content |
| 6 | SQ+SB | Square with sub-oscillator |

### Bank 2: SUB+SYNC
Sub-oscillators and oscillator sync timbres.

| # | Patch | Description |
|---|-------|-------------|
| 1 | SW+SB | Sawtooth with sub-oscillator |
| 2 | SQSNC | Square with hard sync |
| 3 | SWSNC | Sawtooth with hard sync |
| 4 | 3xSAW | Triple detuned sawtooth |
| 5 | 3xSQ | Triple detuned square |
| 6 | 3xTRI | Triple detuned triangle |

### Bank 3: STACK
Stacked oscillators and complex layered timbres.

| # | Patch | Description |
|---|-------|-------------|
| 1 | 3xSIN | Triple detuned sine |
| 2 | 3xRNG | Triple oscillator ring modulation |
| 3 | SWARM | Sawtooth swarm (many detuned oscs) |
| 4 | COMB | Sawtooth through comb filter |
| 5 | TOY | Lo-fi toy keyboard sound |
| 6 | DIGLP | Digital filter (lowpass) |

### Bank 4: FILTER
Digital filter and formant synthesis models.

| # | Patch | Description |
|---|-------|-------------|
| 1 | DIGPK | Digital filter (peak/resonant) |
| 2 | DIGBP | Digital filter (bandpass) |
| 3 | DIGHP | Digital filter (highpass) |
| 4 | VOSIM | VOSIM vocal synthesis |
| 5 | VOWEL | Vowel formant synthesis |
| 6 | FOF | FOF (Forme d'Onde Formantique) |

### Bank 5: FM
FM synthesis and related timbres.

| # | Patch | Description |
|---|-------|-------------|
| 1 | HARMO | Additive harmonics |
| 2 | FM | 2-operator FM |
| 3 | FBKFM | FM with feedback |
| 4 | CHAOS | Chaotic FM feedback |
| 5 | PLUCK | Plucked string (Karplus-Strong) |
| 6 | BOWED | Bowed string physical model |

### Bank 6: PHYSIC
Physical modeling synthesis.

| # | Patch | Description |
|---|-------|-------------|
| 1 | BLOWN | Blown pipe/reed model |
| 2 | FLUTE | Flute physical model |
| 3 | BELL | Struck bell/metallic |
| 4 | DRUM | Struck drum membrane |
| 5 | KICK | Synthetic kick drum |
| 6 | CYMBL | Cymbal/hi-hat |

### Bank 7: WAVES
Wavetable synthesis and hybrid approaches.

| # | Patch | Description |
|---|-------|-------------|
| 1 | SNARE | Synthetic snare drum |
| 2 | WTABL | Classic wavetable |
| 3 | WMAP | 2D wavetable mapping |
| 4 | WLINE | Wavetable with interpolation |
| 5 | WPARA | Paraphonic wavetable (chords) |
| 6 | FNOIS | Filtered noise |

### Bank 8: NOISE
Noise, granular, and experimental synthesis.

| # | Patch | Description |
|---|-------|-------------|
| 1 | TWINS | Twin peaks filtered noise |
| 2 | CLOCK | Clocked digital noise |
| 3 | GRAIN | Granular cloud synthesis |
| 4 | PARTC | Particle/dust noise |
| 5 | DIGMD | Digital modulation artifacts |
| 6 | ???? | Question mark (surprise!) |

## Display Layout

The 64×48 pixel OLED shows:

```
┌────────────────┐
│    ANALOG      │  ← Bank name (inverted when in Bank mode)
│────────────────│
│     CSAW       │  ← Patch name (large, inverted when in Patch mode)
│                │
│   1/8  1/6     │  ← Bank/Patch position
└────────────────┘
```

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

> **Note for the curious.** Joy is a `BOOT_QSPI` build: it runs from the module's external
> QSPI flash and is launched by the bootloader, so the ST-Link/OpenOCD `make program` route
> used for ordinary Daisy programs is intentionally disabled here.

## Calibration

V/Oct calibration can be adjusted by defining these values before `#include`:

```cpp
#define VOCT_BASE_MIDI 48     // MIDI note at 0V (default: C3)
#define VOCT_CENTER_NORM 0.0f // ADC value at 0V (trim offset)
```

## License

**Joy** is MIT-licensed. It is based on Mutable Instruments Braids by Émilie Gillet,
also released under the MIT License — the original copyright notices are retained in
the vendored sources. See the original [Mutable Instruments repository](https://github.com/pichenettes/eurorack) for details.

"Braids" and "Mutable Instruments" are marks of their owner and are used here only to
describe this firmware's origin; "Daisy" is a mark of Electrosmith. Joy is an
independent community work, not affiliated with or endorsed by either.
