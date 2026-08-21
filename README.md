# eurorack_daisy_patch_init

Makefile-based firmware projects built on top of Electrosmith **libDaisy** and **DaisySP**, with some ports that also use Mutable Instruments' **eurorack** sources.

This repo is intended to be self-contained via git submodules (no shared external `../deps` tree).

## Released modules

Firmwares offered as finished modules are named after the nursery rhyme
(*"one for sorrow, two for joy…"* — hence **Eight4aWish**):

| Name | Firmware | Version | Based on | Licence |
| --- | --- | --- | --- | --- |
| **Sorrow** | [`daisy_grids/`](daisy_grids/) | v2.0.0 | Mutable Instruments Grids | **GPL-3.0-or-later** |
| **Joy** | [`daisy_braids_oled/`](daisy_braids_oled/) | v1.4.0 | Mutable Instruments Braids | MIT |
| **Joy Lite** | [`daisy_joy_lite/`](daisy_joy_lite/) | v1.4.0 | Mutable Instruments Braids | MIT |

**Joy** and **Joy Lite** are the same macro-oscillator generation (shared calibration
and DSP, so they carry the same version): Joy is the full 48-model version with an
OLED navigator; Joy Lite is screenless — a curated 16 models (the ones Plaits
doesn't cover) navigated by LED + button + toggle.

These are independent community works for the Electrosmith Daisy Patch Init. Product
names of other makers are used only to describe each firmware's origin — **not
affiliated with, or endorsed by, Mutable Instruments or Electrosmith.**

## Build guides and downloads

Prebuilt `.bin` files, flashing instructions, panel STLs and a wiring guide for each
module: **https://eight4awish.com/modules/**

## Getting in touch

Email **info@eight4awish.com**.

I usually have a few spare front panels over from each print run — if you would like one,
say which module and roughly where you are and I will come back to you on postage.

Questions about building or flashing are best raised as an
[issue](https://github.com/Eight4aWish/eurorack_daisy_patch_init/issues), so the answer
stays available to whoever hits the same thing next.

## Clone

```sh
git clone --recurse-submodules <repo-url>
# or, if already cloned:
git submodule update --init --recursive
```

Submodules are expected at:

- `deps/daisy/libDaisy`
- `deps/daisy/DaisySP`
- `deps/mutable/eurorack`

## Build

These projects link against the static libraries built by `libDaisy` and `DaisySP` (and for some effects, `DaisySP-LGPL`).

You can either build the libraries once up-front:

```sh
make -C deps/daisy/libDaisy
make -C deps/daisy/DaisySP
```

…or just run `make` in a project directory; most project Makefiles will build any missing libraries automatically.

Most projects default to using repo-local deps. Typical usage:

```sh
cd daisy_multiosc
make
```

If you keep Daisy deps elsewhere, you can override on the command line:

```sh
make DAISY_ROOT=/path/to/daisy
```

(That directory must contain `libDaisy/` and `DaisySP/`.)

## Projects

- `daisy_multiosc/` – **boot-selectable multi-engine synth** for the Daisy Patch Init: pick a voice at power-up, all sharing one universal panel + OLED legend (`BOOT_QSPI`). Engines: **FM4OP** (4-op FM), **INTVL** (dual interval oscillator), **SCAN** (scanned synthesis), **BYTEBEAT** (dual-voice bytebeat), plus a SINE test voice. See its [README](daisy_multiosc/README.md), the control contract in [docs/PANEL.md](docs/PANEL.md), and [docs/ROADMAP.md](docs/ROADMAP.md).
- `daisy_grids/` – **Sorrow** — drum machine: a pool of ten DaisySP drum models rolled by the B8 toggle, three pattern banks (one Grids' own, two derived here from open MIDI corpora via a self-organising map), per-part densities and automatic clock-resolution detection, based on Mutable Instruments Grids (**GPL-3.0-or-later**). Runs `BOOT_SRAM` — needs the Daisy bootloader.
- `daisy_braids_oled/` – **Joy** — macro oscillator (48 models) with 64x48 OLED menu navigation, based on Mutable Instruments Braids; on-module V/Oct calibration (hold B7 at power-up)
- `daisy_joy_lite/` – **Joy Lite** — screenless sibling: a curated 16 Braids models (the ones Plaits doesn't cover) in two toggle banks, model number shown by LED blink
- `daisy_multifx_oled/` – Multi-effect processor with 64x48 OLED: 16 effects in a 4×4 grid (Reverb / Delay / Tone / Misc banks), built on the shared `multifx_core` library (also runs on a bare Daisy Seed with a homebrew front end — see `daisy_multifx_seed/`)
- `daisy_fm4op/` – 4-op FM synth (standalone build, and the FM4OP engine for `daisy_multiosc`)
- `daisy_interval_osc/` – **INTVL** — Nick Donaldson's IntervalOsc: dual oscillator with quantized interval offset ([ndonald2/DaisyPatches](https://github.com/ndonald2/DaisyPatches), MIT — see [`daisy_interval_osc/LICENSE-daisypatches.txt`](daisy_interval_osc/LICENSE-daisypatches.txt)). His patch and README are kept as written; the multiosc engine is a port of that DSP onto the universal panel
- `daisy_scanned/` – scanned-synthesis engine source for `daisy_multiosc` (no standalone build)
- `daisy_bytebeat/` – **BYTEBEAT** engine source for `daisy_multiosc` (no standalone build): dual-voice bytebeat with a 100-formula bank in five families and a lo-fi Tone macro, ported from [Ogham](https://github.com/keeos-io/ogham) by Steven Collins (Keeos.io, MIT — see [`daisy_bytebeat/LICENSE-ogham.txt`](daisy_bytebeat/LICENSE-ogham.txt))

## Shared code

- `common/multiosc_core/` – host/framework for `daisy_multiosc`: the `Engine` interface, boot chooser, B7 gestures (short = cycle, long = Play/Edit), soft-takeover, and the OLED legend. See its [README](common/multiosc_core/README.md).
- `common/multifx_core/` – portable, DaisySP-only MultiFX DSP/UI core (reverb, delay, tone and misc effect banks, output voicing, and a Bank/Patch navigation model) shared by `daisy_multifx_oled` (and its homebrew Daisy Seed variant), and intended for reuse on other Daisy boards. See its [README](common/multifx_core/README.md).
- `common/voct_cal.h` + `common/joy_dsp.h` – shared by **Joy** and **Joy Lite**: the two-point V/Oct calibration math, and the envelope + fixed-point DSP helpers. Only each build's front-end (OLED navigator vs screenless LED/toggle) differs.
- `common/` also holds the shared `oled_soft_i2c` SSD1306 driver used by the OLED projects.
- `common/tools/` – host-side **panel checks** (`make check`): builds the real OLED driver and the real legend/menu layout with a PC compiler and asserts nothing falls off the 64x48 screen. The panel fails silently — a row one pixel too low draws nothing at all — so this catches layout bugs without a bench cycle. See its [README](common/tools/README.md).

## Credits & Licenses

This repository combines original code with several third-party ports. Original
code and integration work is MIT-licensed (see [`LICENSE`](LICENSE)). Vendored
and submoduled sources retain their own licenses and copyright headers, which
are kept intact. Names below are trademarks/marks of their respective owners;
the ports here are community works and are not official or endorsed.

| Component | Author / upstream | License |
| --- | --- | --- |
| libDaisy, DaisySP | Electrosmith | MIT |
| DaisySP-LGPL (linked by `daisy_interval_osc`, `daisy_multiosc`) | Electrosmith + upstreams | LGPL-2.1 |
| `daisy_braids_oled` (**Joy**) – Braids DSP | Émilie Gillet (Mutable Instruments, [`eurorack`](https://github.com/pichenettes/eurorack)) | MIT |
| `daisy_interval_osc` (**INTVL**) – IntervalOsc patch + the DSP behind the multiosc engine | Nick Donaldson ([ndonald2/DaisyPatches](https://github.com/ndonald2/DaisyPatches)), after Electrosmith DaisyExamples | MIT ([notice](daisy_interval_osc/LICENSE-daisypatches.txt)) |
| `daisy_bytebeat` (**BYTEBEAT**) – bytebeat engine, formula bank, Tone macro | Steven Collins (Keeos.io, [`ogham`](https://github.com/keeos-io/ogham)) | MIT ([notice](daisy_bytebeat/LICENSE-ogham.txt)) |
| `daisy_grids` (**Sorrow**) – Grids pattern generator | Émilie Gillet (Mutable Instruments) | **GPL-3.0-or-later** |
| `daisy_multiosc` (host + SCAN engine), `daisy_scanned`, `daisy_fm4op`, `daisy_multifx_*`, `common/`, integration and the ports of the above | David Baghurst | MIT |

Notes:

- **Modifications.** Ported sources have been modified for this hardware (for
  example, replacing the panel toggle switch with an OLED screen and on-screen
  control legends). Modifications are noted in the relevant file headers
  alongside the original copyright notices; the original notices are never
  removed.
- **GPL (Sorrow).** Mutable's **Grids is GPL-3.0-or-later** — unlike most of the
  `eurorack` sources, which are MIT. So `daisy_grids` (**Sorrow**) is copyleft:
  distributing it, source or binary, obliges you to pass on the complete
  corresponding source under the GPL. It cannot be relicensed to MIT/CC0.
- **LGPL.** `daisy_interval_osc` links Electrosmith's `DaisySP-LGPL`. Because
  full source is published here, LGPL relinking obligations are satisfied. If
  you distribute prebuilt binaries (e.g. attaching `.bin` files to a release),
  include a pointer to this source so the LGPL portions can be relinked.
- **Trademarks vs. copyright.** The licences above grant rights to the *code*,
  not to product names ("Braids", "Grids", "Mutable Instruments", "Daisy").
  Released modules therefore carry their own names (**Sorrow**, **Joy**); the
  originals are cited only to describe lineage. These are community works and do
  not imply endorsement.
