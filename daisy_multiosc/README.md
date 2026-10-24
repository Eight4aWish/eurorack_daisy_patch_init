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
  four registered in `main.cpp`, in chooser order:
  - `FmFourOp` — [`../daisy_fm4op/fm4op_voice.{h,cpp}`](../daisy_fm4op/)
  - `IntervalOscVoice` — [`../daisy_interval_osc/intervalosc_voice.{h,cpp}`](../daisy_interval_osc/)
  - `ScannedVoice` — [`../daisy_scanned/scanned_voice.{h,cpp}`](../daisy_scanned/)
  - `WavesEngine` — `waves_engine.h` (plain VCO: sine, triangle, saw, square
    with pulse width; grown from the old SINE test voice)

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

Hardware-verified on the Daisy Patch Init for **FM4OP**, **INTVL** and **SCAN**.
**WAVES** (2026-10-24) builds and passes the panel check but is **not yet
bench-tested**.

**BYTEBEAT was taken out on 2026-10-24.** The Ogham in the rack is the module its
code came from, so the engine duplicated hardware already there. It was never
bench-tested in MultiOsc. Its source stays in
[`../daisy_bytebeat/`](../daisy_bytebeat/), unbuilt, so putting it back is its
four sources in the Makefile and one `AddEngine` line.

A fifth engine once exposed two panel bugs, both fixed: the boot menu drew its
last entry off the bottom of the 48 px screen (it now scrolls to keep the
highlighted engine visible, for any number of engines), and a title longer than
ten characters underflowed the centring arithmetic and rendered as a wrapped
fragment (long strings now clip at the right edge instead). Possible follow-ups: tune SCAN's spring constants to
taste; converge the standalone `fm4op.cpp` /
`IntervalOsc.cpp` onto their `*_voice` modules (currently duplicated). Future
"different" engines: see [../docs/ROADMAP.md](../docs/ROADMAP.md).

**Joy and Sorrow stay separate firmwares** on their own SD cards, not engines
here: the host runs every engine at 48 kHz (Joy needs 96 kHz, Sorrow 32 kHz),
`BOOT_QSPI` cannot save Joy's patch and calibration, Joy spends all four knobs
on its voice so it has no TUNE knob, Sorrow's kit roll is on B8, which the OLED
panel lacks, and Sorrow's GPL would cover the whole image.
