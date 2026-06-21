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
cd daisy_fm4op
make
```

If you keep Daisy deps elsewhere, you can override on the command line:

```sh
make DAISY_ROOT=/path/to/daisy
```

(That directory must contain `libDaisy/` and `DaisySP/`.)

## Projects

- `daisy_braids_oled/` – Braids macro oscillator port with 64x48 OLED display and menu navigation (see project README for BOOT_QSPI notes)
- `daisy_grids/` – 3-drum sequencer with Grids-style pattern generator (GPL-3.0)
- `daisy_multifx_oled/` – Multi-effect processor with 64x48 OLED: 16 effects in a 4×4 grid (Reverb / Delay / Tone / Misc banks), built on the shared `multifx_core` library (also runs on a bare Daisy Seed with a homebrew front end — see `daisy_multifx_seed/`)
- `daisy_fm4op/` – 4-op FM synth example for Daisy Patch SM

## Shared code

- `common/multifx_core/` – portable, DaisySP-only MultiFX DSP/UI core (reverb, delay, tone and misc effect banks, output voicing, and a Bank/Patch navigation model) shared by `daisy_multifx_oled` (and its homebrew Daisy Seed variant), and intended for reuse on other Daisy boards. See its [README](common/multifx_core/README.md).
- `common/` also holds the shared `oled_soft_i2c` SSD1306 driver used by the OLED projects.
