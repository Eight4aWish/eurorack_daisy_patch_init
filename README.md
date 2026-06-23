# eurorack_daisy_patch_init

Makefile-based firmware projects built on top of Electrosmith **libDaisy** and **DaisySP**, with some ports that also use Mutable Instruments' **eurorack** sources.

This repo is intended to be self-contained via git submodules (no shared external `../deps` tree).

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

- `daisy_multiosc/` – **boot-selectable multi-engine synth** for the Daisy Patch Init: pick a voice at power-up, all sharing one universal panel + OLED legend (`BOOT_QSPI`). Engines: **FM4OP** (4-op FM), **INTVL** (dual interval oscillator), **SCAN** (scanned synthesis), plus a SINE test voice. See its [README](daisy_multiosc/README.md), the control contract in [docs/PANEL.md](docs/PANEL.md), and [docs/ROADMAP.md](docs/ROADMAP.md).
- `daisy_braids_oled/` – Braids macro oscillator port with 64x48 OLED display and menu navigation (see project README for BOOT_QSPI notes)
- `daisy_grids/` – 3-drum sequencer with Grids-style pattern generator (GPL-3.0)
- `daisy_multifx_oled/` – Multi-effect processor with 64x48 OLED: 16 effects in a 4×4 grid (Reverb / Delay / Tone / Misc banks), built on the shared `multifx_core` library (also runs on a bare Daisy Seed with a homebrew front end — see `daisy_multifx_seed/`)
- `daisy_fm4op/` – 4-op FM synth (standalone build, and the FM4OP engine for `daisy_multiosc`)
- `daisy_interval_osc/` – dual oscillator with quantized interval offset (standalone build, and the INTVL engine)
- `daisy_scanned/` – scanned-synthesis engine source for `daisy_multiosc` (no standalone build)

## Shared code

- `common/multiosc_core/` – host/framework for `daisy_multiosc`: the `Engine` interface, boot chooser, B7 gestures (short = cycle, long = Play/Edit), soft-takeover, and the OLED legend. See its [README](common/multiosc_core/README.md).
- `common/multifx_core/` – portable, DaisySP-only MultiFX DSP/UI core (reverb, delay, tone and misc effect banks, output voicing, and a Bank/Patch navigation model) shared by `daisy_multifx_oled` (and its homebrew Daisy Seed variant), and intended for reuse on other Daisy boards. See its [README](common/multifx_core/README.md).
- `common/` also holds the shared `oled_soft_i2c` SSD1306 driver used by the OLED projects.

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
| `daisy_braids_oled` – Braids DSP | Émilie Gillet (Mutable Instruments, [`eurorack`](https://github.com/pichenettes/eurorack)) | MIT |
| `daisy_interval_osc` – IntervalOsc | Nick Donaldson ([ndonald2/DaisyPatches](https://github.com/ndonald2/DaisyPatches)) | MIT |
| `daisy_grids` | Mutable Instruments (Grids) derivative | GPL-3.0 |
| `daisy_multiosc` (host + SCAN engine), `daisy_scanned`, `daisy_fm4op`, `daisy_multifx_*`, integration | David Baghurst | MIT |

Notes:

- **Modifications.** Ported sources have been modified for this hardware (for
  example, replacing the panel toggle switch with an OLED screen and on-screen
  control legends). Modifications are noted in the relevant file headers
  alongside the original copyright notices; the original notices are never
  removed.
- **LGPL.** `daisy_interval_osc` links Electrosmith's `DaisySP-LGPL`. Because
  full source is published here, LGPL relinking obligations are satisfied. If
  you distribute prebuilt binaries (e.g. attaching `.bin` files to a release),
  include a pointer to this source so the LGPL portions can be relinked.
- **Trademarks vs. copyright.** The MIT licenses above grant rights to the
  *code*, not to product names ("Braids", "Grids", "Mutable Instruments",
  "Daisy"). These ports are community works and do not imply endorsement.
