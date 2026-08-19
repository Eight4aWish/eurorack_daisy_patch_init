# daisy_multiosc — roadmap

A single boot-selectable firmware (working name **`daisy_multiosc`**) hosting
several small synth-voice engines on the hand-built Daisy Patch Init, sharing one
universal panel. Control contract and panel: see [PANEL.md](PANEL.md).

## Structure

Mirrors the existing `common/multifx_core` + `daisy_multifx_oled`/`_seed`
pattern (shared core, thin app wrapper):

- **`common/multiosc_core/`** — host + shared UI: the `Engine` interface
  (`Init / Process / control-map / MOD labels / Draw`), boot menu, OLED legend,
  B7 gesture + Play/Edit page handling, soft-takeover.
- **Each engine keeps its own directory** (`daisy_fm4op/`, `daisy_interval_osc/`,
  `daisy_scanned/`, …). Each exposes its DSP/voice as `*_voice.{h,cpp}`,
  decoupled from any standalone `main.cpp`. The standalone `main` **stays behind a
  build flag** for single-engine DFU testing — and so any engine that proves good
  enough can **graduate to its own dedicated hardware** with no untangling from
  the shared host.
- **`daisy_multiosc/`** — the integrating app: builds `BOOT_QSPI`, compiles the
  engine sources, lists them in the menu. Large buffers belong in SDRAM; audio
  engine re-inits per engine for native sample-rate/block-size.

Provenance stays per-folder so vendored third-party code keeps its own license
and headers (see root README "Credits & Licenses").

## Engines

### Shipped

| Engine | Source | License | Notes |
| --- | --- | --- | --- |
| **FM4OP** | original (DaisySP) | MIT | 4-op FM; short-press = algorithm; edit = Vol/Atk/Rel |
| **INTVL** | ndonald2/DaisyPatches (Donaldson) | MIT | dual osc; interval/detune/PW; mono sum; optional AR env |
| **SCAN** | original | MIT | scanned synthesis; Tension/Damping/Hammer; evolving drone |
| **BEAT** | keeos-io/ogham (Collins) | MIT | dual-voice bytebeat; 100 formulas in 5 families; Tone lo-fi macro; short-press = next formula |
| SINE | original | MIT | test voice |

`daisy_fm4op/` and `daisy_interval_osc/` keep a `BOOT_NONE` standalone build for
single-engine DFU testing; `daisy_scanned/` and `daisy_bytebeat/` are engine-only.

### Future engines — go *different*

The owner's rack already covers the mainstream palette in hardware, so new
engines should be off-the-beaten-path. **Dropped as redundant:** Rings &
Elements (own hardware), additive / Harmoniqs (Plaits covers additive — and
Harmoniqs is unlicensed + needs a modified DaisySP), wavefolder / West-Coast
(Befaco folder + Behringer Waves), granular (Typhoon = Clouds).

Worthwhile "different" candidates, all clean from-scratch MIT builds:

| Engine | Why different |
| --- | --- |
| **Phase distortion (Casio CZ)** | filter-like timbres with no filter; easy, clean 3-macro fit |
| **Vocoder** | analyses the **audio IN** — uses the otherwise-idle input jack |
| **SD sampler / user wavetables** | sampling — the one category the rack lacks; uses the SD slot |
| Wave-terrain | extends SCAN's evolving-shape idea into 2-D |

## Build & flash (dev loop on the braids hardware)

The braids unit is identical hardware and already has the Daisy bootloader
flashed (it runs `BOOT_QSPI`), so `daisy_multiosc` runs on it as-is.

```sh
cd daisy_multiosc
make                       # builds build/daisy_multiosc.bin (BOOT_QSPI)
```

Flash either way:

- **SD card:** copy `build/daisy_multiosc.bin` to the SD root, power-cycle. The
  bootloader scans root for the first `.bin`, and re-flashes QSPI only if it
  differs — so leave it on the card; rebuild + replace the file to iterate.
- **DFU:** `make program-dfu` during the bootloader's ~2.5 s grace period.

SD must be fully wired (4-bit data lines). One SOS LED blink = a bad/incompatible
`.bin` on the media (e.g. a `BOOT_NONE` build).
