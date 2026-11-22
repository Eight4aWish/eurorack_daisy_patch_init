# daisy_scanned — SCAN engine

Scanned-synthesis voice for [`daisy_multiosc`](../daisy_multiosc/). Engine source
only — there is no standalone build; it's compiled into the `daisy_multiosc`
image as **SCAN**, the third engine a long hold of B7 steps to.

## What it is

A ring of spring-coupled masses (a slow, sub-audio "string") is excited by the
gate; its evolving *shape* is read at audio rate as the waveform. Pitch (the scan
rate) is decoupled from the timbral evolution (the spring dynamics), so a held
note morphs organically. Original work (MIT) — see Verplank/Mathews/Shaw scanned
synthesis for the technique.

## Controls (see [../docs/PANEL.md](../docs/PANEL.md))

- **V/Oct** — pitch (scan rate), C3 at 0 V
- **Pot 1 Excitation** — how the ring is struck: Pulse, Bump, Two, Noise
- **Pot 2 Tension** · **Pot 3 Hammer** · **Pot 4 Centring**, each with the MOD jack below it
- **Hold B7 + pot 4: Damping**, its decay. A struck ring has no attack, so hold + pot 3 does nothing
- **Until the first gate** it drones, fed all the time; **from then on** each gate strikes it and it
  rings out by the damping

Spring constants are first-pass and tunable in
[`scanned_voice.cpp`](scanned_voice.cpp).

## Files

- `scanned_voice.{h,cpp}` — the engine (implements the `multiosc::Engine` interface)

Licence: MIT (original work). See the repo root [`LICENSE`](../LICENSE).
