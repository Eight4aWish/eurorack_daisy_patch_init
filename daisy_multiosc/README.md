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
- Engines live in their own folders and are compiled in via the Makefile. All
  five registered in `main.cpp`, in chooser order:
  - `FmFourOp` — [`../daisy_fm4op/fm4op_voice.{h,cpp}`](../daisy_fm4op/)
  - `IntervalOscVoice` — [`../daisy_interval_osc/intervalosc_voice.{h,cpp}`](../daisy_interval_osc/)
  - `ScannedVoice` — [`../daisy_scanned/scanned_voice.{h,cpp}`](../daisy_scanned/)
  - `BytebeatVoice` — [`../daisy_bytebeat/`](../daisy_bytebeat/) (dual-voice
    bytebeat; engine + 100-formula bank vendored from Keeos' Ogham, MIT)
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

Hardware-verified on the Daisy Patch Init for **FM4OP**, **INTVL**, **SCAN** and
the SINE test voice. **BEAT** (bytebeat) is new and **not yet built for the
target or bench-tested** — its DSP is checked host-side
([`../daisy_bytebeat/tools/`](../daisy_bytebeat/tools/)), its control mapping is not. Possible follow-ups: tune SCAN's spring constants to
taste; retire the SINE test voice; converge the standalone `fm4op.cpp` /
`IntervalOsc.cpp` onto their `*_voice` modules (currently duplicated). Future
"different" engines: see [../docs/ROADMAP.md](../docs/ROADMAP.md).
