# Mirth's FX bank: design, for review

**Unreleased.** For David's two OLED patch.inits, built with `make FX=1`. `make` and the release
script still build v1.1.0's three banks. Written 2026-11-11, before any code. Nothing here is built yet.

## Decided (David, 2026-11-11)

- **A fourth bank, FX,** after FILTERS, with eight effects: four reverbs (CLASSIC, PLATE, TANK,
  SHIMMER) and four delays (PING, TAPE, MULTITAP, ECHOVERB). The other eight MultiFX effects stay
  out: Mirth's amps, not-amps and filters already cover drive and filtering.
- **Mirth's way of working, unchanged:** B7 short press for the next effect, held for the next bank.
  No menu, so it stays consistent with Mirth Lite and the published videos.
- **Knob 1 is MIX** on every bank. Knobs 2–4 (MOD1–3 on the faceplates) are each effect's three
  controls.
- **Written properly from scratch,** not ported. "Not wedded to the originals… things do move on."
  The code being replaced is `common/multifx_core`, lifted in June from the 2025 DaisyDuino firmware
  with its constants unchanged.
- **Private for now:** on `main`, not in a release or on the site.

## Platform first

**Done 2026-11-11:** libDaisy v9.0.0 and DaisySP master (`adc30c7`); every patch.init app builds, and Mirth's three banks checked by ear on the module ("all fine").

- **Update the libraries before writing the effects,** as agreed on 29 Oct (after Mirth v1.1.0 shipped):
  - **libDaisy**: v8.0.0 → v9.0.0 (29 Sep) or master. Knobs and CV ins stop reading about 3% low, the
    CV outs reach their full range, and a cache-alignment fix landed on 4 Nov.
  - **DaisySP**: our May 2025 commit → master. The only change is the pitch-shifter fix, which the
    old SHIMMER used.
- **Check Mirth's three banks by ear on the updated build** before the FX bank goes on top. The knob
  positions shift by about 3%.

## Shared design

- **Stereo in, stereo out.** IN_R is normalled to IN_L, so one cable feeds both sides. Mirth's other
  banks stay mono.
- **48 kHz, 48-sample blocks,** as Mirth.
- **MIX is an equal-power crossfade.** The level is the same at noon as at either end.
- **Levels matched across the eight.** At full MIX, a reference signal comes out as loud as it went
  in, measured by the harness, so turning MIX means the same on every effect.
- **Controls read once a block and smoothed;** delay times glide without pitch jumps.
- **Tap tempo on gate in 1** for all four delays. Mirth uses no gates, so it's free.
- **No LGPL code.** The old reverbs used DaisySP-LGPL's `ReverbSc`. New engines are written here:
  MIT, like the rest of Mirth.
- **Switching** uses Mirth's existing fade: out, the delay memory cleared in the main loop while
  muted, then back in. A tail is cut on a change, as now.
- **CPU:** each effect should stay under about 60% on Mirth's readout, leaving room for safety. Each
  is measured on the module.

## The eight

| Effect | What it becomes | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| **CLASSIC** | A dense, smooth hall in the same family as the old one, which was Costello's 8-line feedback delay network. A modern 8-line FDN with gently modulated lines. | Decay | Tone (damping) | Pre-delay (new: the old knob 4 did nothing) |
| **PLATE** | The Dattorro plate (Dattorro, "Effect Design Part 1", JAES 1997), the standard published plate. | Pre-delay | Tone | Decay |
| **TANK** | A large modulated space for long ambient tails: an FDN with longer lines and deeper, slower modulation than CLASSIC. | Size | Damping | Modulation depth |
| **SHIMMER** | A true shimmer: the pitch-shifted tail is **fed back into the reverb**, so it climbs octave by octave. The old one only layered a shifted copy on top. | Decay | Tone | Shimmer amount |
| **PING** | A true ping-pong: the input enters one side and the repeats alternate. The old one fed both sides and crossed the feedback. | Time | Feedback | Damping |
| **TAPE** | A tape echo: wow and flutter (slow, fast and a little random drift), soft saturation and a narrowing band in the feedback loop, so the repeats wear. | Time | Feedback | Wear (wow, flutter, saturation) |
| **MULTITAP** | Rhythmic taps from a set of patterns (straight, dotted, triplet…) spread across the stereo field. | Time | Pattern | Feedback |
| **ECHOVERB** | A delay whose repeats feed CLASSIC's reverb. | Time | Feedback | Reverb amount |

## How it gets built

1. **The library update,** then Mirth's banks by ear on the module.
2. **Done 2026-11-11 (`176687f`), checked on the module ("fine"):** **the bank's plumbing in Mirth** (build switch, fourth bank, switching, knobs, display, stereo,
   tap), with a passthrough in place of the effects, flashed and checked.
3. **Done 2026-11-11, all eight at once (David: "just make the full bank"):** **each effect written and judged on the Mac first.** In `src/fx/`: `dsp.h` (building blocks), `reverb.h` (the FDN behind CLASSIC, TANK, SHIMMER and ECHOVERB's hall; Dattorro's PLATE), `delays.h` (PING, TAPE, MULTITAP, ECHOVERB). On the Mac (`tools/fx_listen.py`), at the knobs' middle, ends and two mixed settings, on the riff and the guitar: no NaNs, no clicks, every tail decaying; the reverbs' RT60 follows the knob; the wet level within about 1.5 dB of the dry at mid settings (louder with long decays and high feedback, by design). The Mac's timings put SHIMMER heaviest (46 ns a sample), CLASSIC 38, ECHOVERB 35, TANK 28, TAPE 20, PLATE 17, MULTITAP 10, PING 4.5.
   **Tap tempo, implemented as proposed:** with a clock on gate in 1, knob 2 on the delays picks 1/16, 1/8, dotted 1/8 or 1/4 of it.
   The old CLASSIC beside the new, matched at 1.93 s, is in `fx_listen/classic/` on the Mac. The old one's onset is a scatter of discrete echoes (28 hits on the click detector), the new one's a smooth wash.
3b. (The original plan for this step:) **Each effect written and judged on the Mac first.** A harness renders it through a guitar riff, a
   synth riff and clicks. It checks for clicks, NaNs, level against the others and stereo, and makes
   short clips to listen to, the old MultiFX version beside the new.
4. **Onto the module,** each one's CPU read from Mirth's display, and David's ear has the last word.

## Open questions

- CLASSIC: aim for its character in a modern build (as proposed), or something new?
- Tap tempo: with a clock in, should knob 2 choose a note division (1/4, dotted 1/8, 1/8, 1/16)
  instead of the time?
- The library update touches every Mirth bank's knob readings by about 3%. Is that fine before the
  FX work?
