# daisy_multifx

A multi-effect processor for Daisy Patch.Init with 4 stereo effects: **Reverb**, **Resonator**, **Delay→Reverb**, and **Granular Pitch Shifter**.

## Features

- **4 stereo effects** selectable via button
- **True stereo** processing throughout
- **External clock sync** for delay time (Effect 3)
- **SDRAM delay buffers** for up to 2 seconds delay
- **License-safe** implementations (no Mutable Instruments code)

## Hardware I/O (Daisy Patch.Init)

### Controls

| Control | Function |
|---------|----------|
| **B7** (Momentary) | Cycle through effects (1-4 LED pulses indicate current effect) |
| **B8** (Toggle) | Edit focus / parameter page select (effect-dependent) |
| **CV1** (Knob) | Parameter 1 (varies by effect) |
| **CV2** (Knob) | Parameter 2 (varies by effect) |
| **CV3** (Knob) | Input level |
| **CV4** (Knob) | Wet/dry mix or send level |

### Audio

| Jack | Function |
|------|----------|
| **Audio In L** | Left input |
| **Audio In R** | Right input |
| **Audio Out L** | Left output (processed) |
| **Audio Out R** | Right output (processed) |

### CV/Gate

| Jack | Function |
|------|----------|
| **CV5** | External clock input (for delay sync in Effect 3) |
| **CV_OUT_2** | LED driver (directly drives panel LED) |

### LED Feedback

- **Effect 1**: 1 pulse per cycle
- **Effect 2**: 2 pulses per cycle
- **Effect 3**: 3 pulses per cycle
- **Effect 4**: 4 pulses per cycle
- **Edit mode (B8 ON)**: LED solid on

---

## Effects

### Effect 0: Reverb

A lush stereo reverb based on DaisySP's `ReverbSc`.

| Knob | Parameter | Range |
|------|-----------|-------|
| CV1 | Decay time | 0.3 - 0.99 (feedback) |
| CV2 | Damping | 1kHz - 19kHz (LP filter) |
| CV3 | Input level | 0 - 100% |
| CV4 | Send to reverb | 0 - 100% |

**Topology**: Send-based. Dry signal passes through, reverb is added on top based on send level.

---

### Effect 1: Resonator

A Rings-inspired modal resonator using bandpass filters. License-safe implementation (no Mutable code).

| Knob | Parameter | Range |
|------|-----------|-------|
| CV1 | Base frequency | 60Hz - 1200Hz (log) |
| CV2 | Damping / Brightness | Low = dark, High = bright |
| CV3 | Input level | 0 - 100% |
| CV4 | Wet/dry mix | 0 - 100% |

**How it works**: Two bandpass filters per channel (fundamental + 5th partial at 1.5× frequency). Input excites the resonators with envelope following for dynamic response. Brightness parameter crossfades between partials.

---

### Effect 2: Delay → Reverb

A delay line feeding into reverb, with external clock sync capability.

#### B8 OFF (Delay focus)
| Knob | Parameter | Range |
|------|-----------|-------|
| CV1 | Delay time | 20ms - 2000ms |
| CV2 | Delay feedback | 0 - 85% |
| CV3 | Input level | 0 - 100% |
| CV4 | Wet/dry mix | 0 - 100% |

*Reverb uses defaults: decay=0.6, damp=8kHz*

#### B8 ON (Reverb focus)
| Knob | Parameter | Range |
|------|-----------|-------|
| CV1 | Reverb decay | 0.3 - 0.99 |
| CV2 | Reverb damping | 1kHz - 19kHz |
| CV3 | Input level | 0 - 100% |
| CV4 | Wet/dry mix | 0 - 100% |

*Delay uses defaults: time=200ms, feedback=45%*

#### External Clock Sync

When a clock signal is present on **CV5**:
- Delay time automatically syncs to the clock interval
- Clock must be active within the last 2 seconds
- Only applies when B8 is OFF (delay focus mode)

**Tip**: Send a tempo-synced LFO or clock divider to CV5 for rhythmic delays.

---

### Effect 3: Granular Pitch Shifter

A simple granular pitch shifter with Hann-windowed grains.

| Knob | Parameter | Range |
|------|-----------|-------|
| CV1 | Pitch shift | -12 to +12 semitones |
| CV2 | Grain size / Density | 25ms-150ms / 40%-85% overlap |
| CV3 | Input level | 0 - 100% |
| CV4 | Wet/dry mix | 0 - 100% |

**How it works**: 
- Audio is written to a circular buffer (~0.5s per channel)
- Grains are read back at a different rate (pitch shift)
- Hann window smooths grain boundaries
- Density controls grain overlap/respawn probability

**Character**:
- Small shifts (±2-3 semitones): Subtle detuning/thickening
- Medium shifts (±5-7 semitones): Harmony generation
- Large shifts (±12 semitones): Octave up/down effects

---

## Memory Usage

This project uses **SDRAM** for large buffers:

| Buffer | Size | Purpose |
|--------|------|---------|
| `delayL` | 96,000 samples (~2s) | Left delay line |
| `delayR` | 96,000 samples (~2s) | Right delay line |
| `g_bufferL` | 24,000 samples (~0.5s) | Left granular buffer |
| `g_bufferR` | 24,000 samples (~0.5s) | Right granular buffer |

Total SDRAM usage: ~480KB

---

## Building

```bash
cd daisy_multifx
make clean && make -j4
```

## Flashing

Put Patch.Init in DFU mode (hold BOOT while pressing RESET), then:

```bash
make program-dfu
```

---

## Technical Details

### Signal Flow

```
                    ┌─────────────┐
Audio In L/R ──────►│   Effect    │──────► Audio Out L/R
                    │  (stereo)   │
                    └─────────────┘
                          │
CV1-4 ───────────────────►│ Parameters
B7 ──────────────────────►│ Effect select
B8 ──────────────────────►│ Edit focus
CV5 ─────────────────────►│ Clock sync (Effect 2 only)
```

### Clock Detection

- Uses hysteresis (thresholds: 0.35 / 0.65) to avoid noise triggering
- Measures interval between rising edges
- Times out after 2 seconds of no edges

### Filter Implementation

The resonator uses DaisySP's `Svf` (state variable filter) in bandpass mode, which is computationally efficient and provides good resonance characteristics.

---

## Potential Improvements

- [ ] Add OLED display support (show effect name)
- [ ] Add tap tempo for delay
- [ ] Add more effects (chorus, phaser, etc.)
- [ ] Add preset save/recall via SD card
- [ ] Add CV modulation of parameters
