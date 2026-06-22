# multiosc_core

Shared host/framework for [`daisy_multiosc`](../../daisy_multiosc/) — mirrors the
`multifx_core` pattern (reusable core + thin app wrapper).

- [`engine.h`](engine.h) — the `Engine` interface every voice implements, the
  universal ADC control map (`KNOB_*`, `CV_*`), `EngineContext` (Play/Edit page +
  gesture flags), and `SoftTakeover`.
- [`host.h`](host.h) / [`host.cpp`](host.cpp) — owns the Daisy Patch SM, OLED and
  B7 button; runs the boot chooser; starts audio; decodes B7 gestures
  (short = cycle, long = Play/Edit); draws the per-engine control legend.

See [../../docs/PANEL.md](../../docs/PANEL.md) for the control contract and
[../../docs/ROADMAP.md](../../docs/ROADMAP.md) for structure rationale.
