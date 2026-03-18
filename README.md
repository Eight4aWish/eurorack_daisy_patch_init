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
cd FM40p
make
```

If you keep Daisy deps elsewhere, you can override on the command line:

```sh
make DAISY_ROOT=/path/to/daisy
```

(That directory must contain `libDaisy/` and `DaisySP/`.)

## Projects

- `daisy_braids/` – Braids macro oscillator port (see project README for BOOT_QSPI notes)
- `daisy_braids_oled/` – Braids port with 64x48 OLED display and menu navigation
- `daisy_grids/` – 3-drum sequencer with Grids-style pattern generator (GPL-3.0)
- `daisy_multifx/` – Multi-effect processor (Reverb, Resonator, Delay→Reverb, Granular)
- `daisy_multifx_oled/` – Multi-effect processor with OLED display
- `FM40p/` – 4-op FM synth example for Daisy Patch SM
- `interval_osc/` – Dual oscillator with interval offset
- `torus_mi/` – Torus string synth port using Mutable `stmlib` sources

### daisy_braids V/Oct tuning overrides

`daisy_braids` supports make-time overrides for the V/Oct calibration defaults:

- `VOCT_BASE_MIDI` – MIDI note at 0V (common conventions: C2=36, C3=48, C4=60)
- `VOCT_CENTER_NORM` – normalized ADC value corresponding to 0V (typically ~0.5 for bipolar inputs)

Examples:

```sh
cd daisy_braids

# 0V = C2
make VOCT_BASE_MIDI=36

# Center trim (example)
make VOCT_CENTER_NORM=0.497f

# Both
make VOCT_BASE_MIDI=48 VOCT_CENTER_NORM=0.5f
```
