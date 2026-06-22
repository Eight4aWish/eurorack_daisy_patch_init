# daisy_multiosc

Boot-selectable multi-voice firmware for the hand-built Daisy Patch Init. Hosts
several small voice engines behind one universal panel, so a single labelled
panel works for every engine. Control contract and panel layout:
[../docs/PANEL.md](../docs/PANEL.md); engine roadmap: [../docs/ROADMAP.md](../docs/ROADMAP.md).

## How it works

- At power-on the OLED shows a chooser. Turn the **TUNE** knob to scroll, press
  **B7** to select. (Auto-select when only one engine is registered.)
- In an engine: **B7 short** = cycle (algorithm / model / waveform); **B7 long**
  = toggle **Play ↔ Edit** page. The screen shows the live meaning of MOD 1–3.
- Engine selection is boot-time only, so B7 never competes with the chooser.

## Structure

- Host/framework: [`../common/multiosc_core/`](../common/multiosc_core/)
  (`Engine` interface, boot menu, B7 gestures, OLED legend, soft-takeover).
- Engines live in their own folders and are compiled in via the Makefile:
  - `FmFourOp` — [`../daisy_fm4op/fm4op_voice.{h,cpp}`](../daisy_fm4op/)
  - `SineEngine` — `sine_engine.h` (test tone; proves the chooser)

To add an engine: implement `multiosc::Engine` in its folder, add its source to
`CPP_SOURCES`, and `host.AddEngine(&inst)` in `main.cpp`.

## Build & flash

```sh
make                 # build/daisy_multiosc.bin (APP_TYPE=BOOT_QSPI)
make program-dfu     # flash via DFU during the bootloader grace period
```

Or drag `build/daisy_multiosc.bin` onto the SD card root and power-cycle. See
[../docs/ROADMAP.md](../docs/ROADMAP.md) for the full dev loop.

## Status

Scaffold: framework + boot menu + fm4op engine + sine test engine, building for
BOOT_QSPI. Not yet hardware-verified. Next: fold in interval_osc and torus;
convert the standalone `fm4op.cpp` to share `fm4op_voice` (currently duplicated).
