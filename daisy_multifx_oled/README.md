# daisy_multifx_oled

Multi-effect processor for Daisy Patch.Init with 64x48 OLED display. This is a variant of [daisy_multifx](../daisy_multifx/) that adds visual feedback and menu navigation via a small SSD1306 OLED.

## Features

- **4 stereo effects** with OLED parameter display
- **2x2 grid menu** for effect selection (long-press B7)
- **Short-press B7** to cycle effects sequentially
- **Per-effect parameter labels** shown on screen
- **External clock sync** for delay time (Effect 2)
- **SDRAM delay buffers** for up to 2 seconds delay

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

- **Short Press:** Cycle to next effect
- **Long Press (500ms):** Toggle 2x2 effect selection menu

### Knobs

Parameters vary per effect — labels are shown on the OLED display.

| Knob | Effect 0: REVERB | Effect 1: RESONATR | Effect 2: DLY+REV | Effect 3: GRANULAR |
|------|-------------------|---------------------|--------------------|--------------------|
| CV_1 | Time (decay) | Freq (base) | DTim (delay time) | Ptch (pitch shift) |
| CV_2 | Damp (LP filter) | Damp (brightness) | Fdbk (feedback) | Size (grain size) |
| CV_3 | InLv (input level) | InLv (input level) | InLv (input level) | InLv (input level) |
| CV_4 | Send (reverb send) | Mix (wet/dry) | Mix (wet/dry) | Mix (wet/dry) |

### CV/Gate

| Jack | Function |
|------|----------|
| **B10** (Gate In 1) | External clock input (delay sync, Effect 2) |
| **CV_OUT_2** | LED driver (blinks on effect change) |

### Audio

| Jack | Function |
|------|----------|
| **Audio In L/R** | Stereo input |
| **Audio Out L/R** | Stereo output (processed) |

## Display Layout

### Effect Mode (default)

```
┌────────────────┐
│    REVERB      │  ← Effect name (centered)
│────────────────│
│  Time  Damp    │  ← CV_1, CV_2 labels
│  InLv  Send    │  ← CV_3, CV_4 labels
└────────────────┘
```

### Menu Mode (long-press B7)

```
┌───────┬────────┐
│  REV  │  RESO  │
├───────┼────────┤
│  D+R  │  GRAN  │
└───────┴────────┘
```

Current effect is highlighted (inverted). Short-press B7 in menu mode cycles through effects.

## Effects

### Effect 0: REVERB
Stereo reverb using DaisySP's `ReverbSc`. Send-based topology — dry signal passes through, reverb is added on top.

### Effect 1: RESONATR
Rings-inspired modal resonator using bandpass filters (fundamental + 5th partial at 1.5×). License-safe implementation (no Mutable Instruments code).

### Effect 2: DLY+REV
Delay line feeding into reverb. Supports external clock sync on B10 — delay time locks to clock interval when a signal is present within the last 2 seconds.

### Effect 3: GRANULAR
Granular pitch shifter with Hann-windowed grains. Pitch range ±12 semitones, grain size 25ms–150ms.

## Changes from daisy_multifx

| Feature | daisy_multifx | daisy_multifx_oled |
|---------|---------------|--------------------|
| Display | None (LED only) | 64x48 OLED |
| Effect select | B7 cycles + LED pulse count | B7 short/long press + OLED menu |
| Parameter page (B8) | Toggle for DLY+REV params | Not used (display shows labels) |
| Parameter feedback | None (blind knob twisting) | Labels shown on screen |

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
