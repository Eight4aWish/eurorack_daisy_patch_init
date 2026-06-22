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
- **Each engine keeps its own directory** (`daisy_torus/`, `daisy_interval_osc/`,
  `daisy_fm4op/`, …). Refactor each so its DSP/voice (`*_voice.{h,cpp}`) is
  decoupled from any standalone `main.cpp`. The standalone `main` **stays behind a
  build flag** for single-engine DFU testing — and so any engine that proves good
  enough can **graduate to its own dedicated hardware** with no untangling from
  the shared host.
- **`daisy_multiosc/`** — the integrating app: builds `BOOT_QSPI`, compiles the
  engine sources, lists them in the menu. Large buffers (e.g. Torus reverb) in
  SDRAM; audio engine re-inits per engine for native sample-rate/block-size.

Provenance stays per-folder so vendored third-party code keeps its own license
and headers (see root README "Credits & Licenses").

## Engines

### Shipped (standalone today, to be folded in)

| Engine | Source | License | Panel fit | Notes |
| --- | --- | --- | --- | --- |
| Torus (Rings) | MI `eurorack` (Gillet) + Sergentanis port | MIT | good | resonator; Damping→Edit; string-synth = separate entry |
| interval_osc | ndonald2/DaisyPatches (Donaldson) | MIT | good | already wired TUNE+V/Oct + MOD CV |
| fm4op | original (DaisySP) | MIT | good | edit = A/R/Vol; short-press = algorithm |

### Candidate oscillators / voices to add

| Engine | Source | License | Effort | Panel fit | Notes |
| --- | --- | --- | --- | --- | --- |
| **Plaits** | MI `eurorack/plaits` (have source); or adopt "Plaitsy"/Mutable Daisies | MIT | med | **excellent** | native Harmonics/Timbre/Morph + model → maps 1:1 to TUNE+MOD1-3+short-press. Highest value. |
| Daisy-Harmoniqs | Krakenpine (native Daisy) | check repo | low | good | additive 6–10 voice, 8 partials each; no MI port needed |
| Stages harmonic osc | MI `eurorack/stages` easter egg | MIT | med-high | fair | six harmonic voices; the "easter egg as discrete patch" idea |
| Warps easter-egg osc | MI `eurorack/warps` | MIT | low-med | good | sine / 3-harmonic / random-harmonic oscillator |
| Tides | MI `eurorack/tides` (have source); "Freshets" fw for ideas | MIT | med | good | osc/LFO; slope/shape/smoothness → MOD1-3 |
| Marbles | MI `eurorack/marbles` | MIT | high | poor (I/O) | random CV/gates, not audio — needs gate-out/CV-out profile; maybe its own utility patch |

Notes:
- Everything Mutable is MIT — port from the vendored `deps/mutable/eurorack`
  submodule for clean provenance; treat others' ports as reference for UI/integration.
- Braids/Grids/MultiFX are intentionally **out of scope** (dedicated hardware).

## Suggested order

1. Build `common/multiosc_core` host + boot menu; fold in **fm4op** first (simplest, already B7-based) as the proof.
2. Fold in **interval_osc**, then **Torus**.
3. Add **Plaits** (best panel fit, highest value).
4. Add a "harmonic oscillator" — pick one of Daisy-Harmoniqs (no port) / Warps egg / Stages egg.
5. Optional: Tides; Marbles as a separate utility build.

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

## Prior art to review first

- "Plaitsy" / **Mutable Daisies** (Ducktronics) — MI modules on patch.init() with a
  common UI; closest existing analogue to this project. Study before finalising
  the dispatcher/menu.
- `hemmer/PlaitsPatchInit` — earlier WIP Plaits port.
