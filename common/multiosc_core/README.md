# multiosc_core

Shared host/framework for [`daisy_multiosc`](../../daisy_multiosc/) — mirrors the
`multifx_core` pattern (reusable core + thin app wrapper).

- [`engine.h`](engine.h) — the `Engine` interface every voice implements; `Controls`,
  what the host hands an engine each block (the four pots with their CV added, the
  note, the gate, the tap, the decay); the ADC map (`POT1`–`POT4`, `CV_VOCT`,
  `CV_POT2`–`CV_POT4`); `Stepper` for pot 1's stepped choices; `SoftTakeover`.
  Engines read no hardware.
- [`host.h`](host.h) / [`host.cpp`](host.cpp) — owns the Daisy Patch SM, OLED and
  B7; runs the V/OCT calibration when B7 is held at power-up; starts the engine it
  was left in; decodes B7 (tap = the engine's, hold and turn = the second bank,
  hold 2 s without turning = the next engine); applies Joy's envelope
  (`joy::AdEnvelope`) and the volume; saves the engine and each engine's tap and
  second bank to QSPI; draws the legend.
- [`gestures.h`](gestures.h) — B7's tap, hold-and-turn and hold for the next engine,
  decoded from the switch's edges; pure, so [`../tools/gesture_check.cpp`](../tools/)
  runs it against libDaisy's debounce.
- [`legend_layout.h`](legend_layout.h) — how the title and the knob grid are
  composed, shared with [`../tools/panel_check.cpp`](../tools/) (`make check`).

See [../../docs/PANEL.md](../../docs/PANEL.md) for the control contract and
[../../docs/ROADMAP.md](../../docs/ROADMAP.md) for structure rationale.
