# daisy_scanned — SCAN engine

Scanned-synthesis voice for [`daisy_multiosc`](../daisy_multiosc/). Engine source
only — there is no standalone build; it's compiled into the `daisy_multiosc`
image and selected from its boot menu as **SCAN**.

## What it is

A ring of spring-coupled masses (a slow, sub-audio "string") is excited by the
gate; its evolving *shape* is read at audio rate as the waveform. Pitch (the scan
rate) is decoupled from the timbral evolution (the spring dynamics), so a held
note morphs organically. Original work (MIT) — see Verplank/Mathews/Shaw scanned
synthesis for the technique.

## Controls (universal panel — see [../docs/PANEL.md](../docs/PANEL.md))

- **TUNE + V/Oct** — pitch (scan rate)
- **MOD1 Tension** · **MOD2 Damping** · **MOD3 Hammer**
- **Short press** — cycle excitation shape (Pulse / Bump / Two / Noise)
- **Edit page** — MOD1 = Centering
- **Env off** (default) = continuous self-excitation → evolving drone;
  **env on** = gated pluck that rings out per Damping

Spring constants are first-pass and tunable in
[`scanned_voice.cpp`](scanned_voice.cpp).

## Files

- `scanned_voice.{h,cpp}` — the engine (implements the `multiosc::Engine` interface)

Licence: MIT (original work). See the repo root [`LICENSE`](../LICENSE).
