# daisy_chaos: Secret

> **Status: running on the Alchemy Lab since 2026-09-30.** First bench session:
> sound and the gated envelope work. GATE moved from J6 to J4, and Drone now
> ignores GATE, because its re-seed clicked on every sequenced note. The TAME
> listening tests, V/Oct accuracy and the scope check (steps 3, 4 and 6 below)
> are still to do. The plan and the measurements behind it are in
> [`docs/SECRET.md`](../docs/SECRET.md).

A chaotic oscillator for the **Hermetic Modular Alchemy Lab (V2)**. It runs
continuous strange attractors as audio, with V/Oct, and a **TAME** control that
goes from free chaos to a locked note without losing the grit in between.

*Seven for a secret, never to be told.* **Secret** is the release name. It
succeeds the Teensy 4.1 **Chaos** module in
[`eurorack_modules`](https://github.com/Eight4aWish/eurorack_modules), whose
firmware is frozen and whose hardware will be repurposed.

## Panel

| Control | Job |
| --- | --- |
| P1 **TUNE** | 27.5–880 Hz, exponential; plus V/OCT on J3 |
| P2 **CHAOS** | the bifurcation parameter; plus CV on J5 |
| P3 **CHAR** | the secondary parameter |
| P4 **TAME** | free chaos (0) to a locked note (1); plus CV on J6 |
| P5 **AD** | envelope attack + decay |
| P6 **SR** | envelope sustain + release |
| B1 | model: Rössler, Van der Pol, Lorenz, Chua, Duffing, Coupled Rössler |
| B2 | envelope: Drone (VCA open) or Gated by J4 |
| B3 | TAME mode: Auto, Force, Sync (Auto is the model's own choice) |

| Jack | Job |
| --- | --- |
| J3 | V/OCT in |
| J4 | GATE in, above +1.2 V. In Gated mode a rising edge opens the envelope and re-seeds the attractor, as on the Teensy. Drone ignores it: a re-seed with the VCA open clicks |
| J5 | CHAOS CV in |
| J6 | TAME CV in |
| J7 / J8 | X / Y CV out: the raw attractor, for a scope (Tiliqua `xbeam`) |
| J9 / J10 | audio L (X) / R (Y) |

CV into CHAOS and TAME: ±5 V sweeps the knob from its centre to either end. The
rings show knob plus CV. Each button's LED shows its current choice by colour.

Boot gestures belong to the board: **B3** held at power-on enters DFU for
flashing, and **B1 + B2** runs the factory CV calibration.

Not in this first build: presets, EXT DRIVE (J1), SYNC in (J2), FREEZE, and a
V/Oct calibration of its own (see below).

## Build

Like the other apps here, a standard Daisy Makefile, laid out after Hermetic's
[`alchemy-template`](https://github.com/hermetic-modular/alchemy-template). The
difference: it builds against the **SDK's own pinned libDaisy**
(`deps/alchemy-sdk/vendor/libDaisy`), not `deps/daisy/libDaisy`. They are
different commits, and this is the one the SDK is tested against.

```sh
git submodule update --init --recursive deps/alchemy-sdk
cd daisy_chaos
make libdaisy      # once
make               # build/secret.bin
```

Needs `arm-none-eabi-gcc` 12 or later. The newlib "`_close` is not implemented"
link warnings are normal: the SDK's own template gives the same eight.

## Flash

1. Connect the front-panel USB-C.
2. Power on while holding **B3**. The rings spin a warm-white comet, then breathe
   slowly: the module is in DFU mode.
3. `make program-dfu` (needs `dfu-util`), or load `build/secret.bin` in the
   [Hermetic Modular Web Programmer](https://hermeticmodular.com/program).

The template's `make program-live` (reflashing without the button) needs the
firmware to run HostLink, and Secret doesn't yet. Use DFU.

## First time on the bench

In order, so a failure points at one thing:

1. **It boots.** The rings show the six knob positions, and B1's LED is orange
   (Rössler).
2. **Sound.** J9/J10 into the mixer, TUNE at noon (~155 Hz), TAME at 0: Rössler's
   rough, pitched drone.
3. **TAME.** Turn it up: free, then locked but still gritty, then a clean
   periodic tone at the top. Try each model on B1, and B3 to compare Force and
   Sync. These are the listening tests from `SECRET.md` section 2.
4. **V/Oct.** With TAME at 1 (strictly periodic, so a tuner can read it), feed J3
   from a quantiser (Scales) and check octaves. TUNE trims the offset. If
   octaves come out consistently stretched or squeezed, that's the input gain,
   and the reason for the calibration below.
5. **Gate.** B2 to Gated, a clock or gate into J4, AD and SR to taste.
6. **Scope.** J7/J8 into Tiliqua's `xbeam` as X/Y.

The Seed's own LED lights if the load governor ever holds the pitch back. It may
not be visible behind the panel. If a high note sounds flat on one model, that's
the governor, and that model's `maxStepsPerSecond` needs retuning for this board.

## Known limits of this build

- **V/Oct accuracy.** The board's factory calibration measures each jack's zero
  point, but its gain is a design constant (`CvInput::SetCalibration`), so the
  scale is only as good as the resistors. A two-point calibration of our own,
  like Joy's `common/voct_cal.h`, comes next if step 4 shows it's needed.
- **CV is read at 1 kHz** through the board's smoothed `AnalogControl`, not at
  audio rate. That's fine for notes and modulation, but not for audio-rate FM.
- **X/Y CV updates once per audio block** (2 kHz), not per sample.
- **No presets.** The knobs are physical; the three button choices reset at power-on.
