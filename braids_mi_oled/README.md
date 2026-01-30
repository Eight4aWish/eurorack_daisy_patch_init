# Braids MI OLED

Mutable Instruments Braids macro oscillator port for Daisy Patch.Init with OLED display for intuitive patch navigation.

## Overview

This is a variant of braids_mi that adds a 64x48 OLED display for visual feedback and two-level menu navigation. The control scheme has been redesigned to use all four knobs for sound parameters, with patch selection handled via the B7 button.

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

```bash
cd braids_mi_oled
make
```

## Flashing

```bash
# Via USB (DFU mode)
make program-dfu

# Via ST-Link/SWD
make program
```

## Calibration

V/Oct calibration can be adjusted by defining these values before `#include`:

```cpp
#define VOCT_BASE_MIDI 48     // MIDI note at 0V (default: C3)
#define VOCT_CENTER_NORM 0.0f // ADC value at 0V (trim offset)
```

## Changes from braids_mi

| Feature | braids_mi | braids_mi_oled |
|---------|-----------|----------------|
| Display | None (LED only) | 64x48 OLED |
| Bank select | B7 button cycles | B7 long-press enters bank mode |
| Patch select | CV_4 knob | B7 short-press cycles |
| Page toggle | B8 toggle switch | N/A (removed for display) |
| Level control | CV_3 knob (or shift) | Fixed level |
| Attack/Decay | CV_1/CV_2 (shifted) | CV_3/CV_4 (always visible) |

## License

Based on Mutable Instruments Braids, released under the MIT License.
See the original [Mutable Instruments repository](https://github.com/pichenettes/eurorack) for details.
