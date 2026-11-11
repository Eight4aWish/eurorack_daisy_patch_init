// Noise test schedule. Keep identical to eurorack_modules/src/noise_test/noise_schedule.h:
// the analysis (eurorack_electronics/tools/noise_test/analyse.py) assumes this layout.
//
// 48 kHz, 48-sample blocks on every board (David, 2026-11-11: "feels fairest").
//
//   30 s of silence (time to power up and start the recording; the ES-10 relocks), then three
//   takes. Each take:
//     sync   0.25 s  1500 Hz at -6 dBFS, so the analysis can find the take
//     gap    1.75 s  silence
//     then for each screen state (off, still, redrawing; off only on a board without a screen):
//       zero     20 s  digital silence out, input ignored      -> the output stage alone
//       tone     10 s  997 Hz at -6 dBFS out                    -> the level reference
//       through  20 s  input copied to output, unity            -> input plus output
//   then silence for good.
//
// The screen state is read by the main loop, which drives the panel; the audio side only
// publishes it.

#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace noise {

constexpr uint32_t FS = 48000;
constexpr int TAKES = 3;
constexpr float LEVEL = 0.5f;  // -6 dBFS
constexpr float SYNC_HZ = 1500.f, TONE_HZ = 997.f;

enum Kind : uint8_t { ZERO, SYNC, TONE, THROUGH, END };
enum Screen : uint8_t { OFF, STILL, REDRAW };

struct Seg {
  Kind kind;
  Screen screen;
  uint32_t samples;
};

class Player {
 public:
  void Begin(bool has_screen) {
    n_ = 0;
    Add(ZERO, OFF, 30.f);
    for (int t = 0; t < TAKES; t++) {
      Add(SYNC, OFF, 0.25f);
      Add(ZERO, OFF, 1.75f);
      for (int s = 0; s < (has_screen ? 3 : 1); s++) {
        Add(ZERO, (Screen)s, 20.f);
        Add(TONE, (Screen)s, 10.f);
        Add(THROUGH, (Screen)s, 20.f);
      }
    }
    Add(END, OFF, 0.f);
    i_ = 0;
    pos_ = 0;
    phase_ = 0.f;
    screen = seg_[0].screen;
  }

  // One block, both channels. inL/inR may be null on a board that has no input buffer.
  void Process(const float *inL, const float *inR, float *outL, float *outR, size_t n) {
    for (size_t k = 0; k < n; k++) {
      const Seg &s = seg_[i_];
      float l = 0.f, r = 0.f;
      switch (s.kind) {
        case SYNC:
        case TONE: {
          float v = LEVEL * sinf(6.2831853f * phase_);
          phase_ += (s.kind == SYNC ? SYNC_HZ : TONE_HZ) / FS;
          if (phase_ >= 1.f) phase_ -= 1.f;
          l = r = v;
          break;
        }
        case THROUGH:
          l = inL ? inL[k] : 0.f;
          r = inR ? inR[k] : 0.f;
          break;
        default:
          break;
      }
      outL[k] = l;
      outR[k] = r;
      if (s.kind != END && ++pos_ >= s.samples) {
        i_++;
        pos_ = 0;
        phase_ = 0.f;
        screen = seg_[i_].screen;
      }
    }
  }

  bool Done() const { return seg_[i_].kind == END; }

  volatile Screen screen = OFF;

 private:
  void Add(Kind k, Screen s, float sec) { seg_[n_++] = {k, s, (uint32_t)(sec * FS + 0.5f)}; }

  Seg seg_[48];
  int n_ = 0, i_ = 0;
  uint32_t pos_ = 0;
  float phase_ = 0.f;
};

}  // namespace noise
