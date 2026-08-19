# Host-side checks

Compile the module's own DSP with a PC compiler and assert things about it. No
hardware, no ARM toolchain, no flashing — this is where a mistake gets caught
before it costs a bench cycle.

```sh
make && ./bank_check && ./voice_check
```

- **`bank_check`** exercises the vendored engine on its own: that the bank is 101
  slots with families at 0/20/40/60/80, that formulas produce full-scale varying
  output, that the A440 reference really is 440 Hz, and that A/B grid
  interpolation neither mutes nor overflows.
- **`voice_check`** drives `BytebeatVoice` through `stub/`, a stand-in for the
  slice of libDaisy the engine touches (ADC reads and a gate). It covers the
  Play/Edit control map, bank cycling, `Selection()` pointer stability — which
  the host's redraw check depends on — and the output DC blocker.

`stub/` exists only for these checks. It is never compiled into firmware; the
real headers come from libDaisy.
