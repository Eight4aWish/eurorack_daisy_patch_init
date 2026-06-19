// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / delay_bank.h
// The four voiced delays from the Daisy Seed MultiFX:
//   Ping     – ping-pong delay
//   Tape     – wow/flutter modulation + low-passed feedback (analog character)
//   MultiTap – three panned taps (0.5x / 1.0x / 1.5x)
//   EchoVerb – low-passed-feedback delay into a reverb
//
// The Tape/EchoVerb feedback-path low-pass and the smoothed delay times are the
// details that make these sound musical rather than harsh/digital.
//
// Memory: holds two 96 000-sample DelayLines (~2 s @ 48 k) plus a ReverbSc for
// EchoVerb. Declare with DSY_SDRAM_BSS in the shell:
//     DSY_SDRAM_BSS static mfx::DelayBank g_delay;
//
// Tap tempo is a host concern (it comes from a gate/CV input), so Ping/EchoVerb
// take a `tap_active` flag and a `tap_samps` value; pass {false, 0} to use P2.
//
// Dependency: DaisySP (DelayLine, ReverbSc).

#pragma once
#include "daisysp.h"
#include "daisysp-lgpl.h" // ReverbSc (EchoVerb) lives in DaisySP-LGPL
#include "dsp_helpers.h"

namespace mfx {

enum class DelayMode { Ping, Tape, MultiTap, EchoVerb };

class DelayBank {
 public:
  void Init(float samplerate) {
    sr_ = samplerate;
    dlyL_.Init(); dlyR_.Init();
    verb_.Init(sr_);
    tS_.Init();
    Reset(DelayMode::Ping);
  }

  // Call on patch change.
  void Reset(DelayMode /*mode*/) {
    dlyL_.Reset(); dlyR_.Reset();
    fb_lpL_ = fb_lpR_ = 0.f;
    tape_ph_ = 0.f;
    verb_.Init(sr_);
    tS_.primed = false;
  }

  // p1 = delay time, p2 = feedback, p3 = per-mode third param (see each case).
  void Process(DelayMode mode, float dryL, float dryR,
               float p1, float p2, float p3,
               bool tap_active, float tap_samps, float& wetL, float& wetR) {
    switch (mode) {
      case DelayMode::Ping: { // p3 = feedback damping (0 = bright, 1 = dark)
        float targ = tap_active
            ? clampf(tap_samps, 10.f, kMax)
            : clampf(map_exp01(p1, 10.f, 800.f) * 0.001f * sr_, 10.f, kMax);
        float t = tS_.Process(targ);
        dlyL_.SetDelay(t); dlyR_.SetDelay(t);
        float dl = dlyL_.Read(), dr = dlyR_.Read();
        float fb = clampf(p2, 0.f, 0.90f);
        // Low-pass the feedback path; tone_a = 1 leaves it untouched (clean).
        float tone_a = map_lin01(p3, 1.0f, 0.10f);
        onepole_lp(dl, dr, tone_a, fb_lpL_, fb_lpR_);
        dlyL_.Write(dryL + fb_lpR_ * fb); // cross-feed (ping-pong)
        dlyR_.Write(dryR + fb_lpL_ * fb);
        wetL = dl; wetR = dr;
      } break;

      case DelayMode::Tape: { // p3 = wow/flutter depth
        float base_ms = map_exp01(p1, 20.f, 800.f);
        tape_ph_ += 0.6f / sr_; if (tape_ph_ >= 1.f) tape_ph_ -= 1.f;
        float depth = map_lin01(p3, 0.f, 0.01f);
        float mod   = 1.f + depth * sin01(tape_ph_);
        float targ = clampf(base_ms * mod * 0.001f * sr_, 10.f, kMax);
        float t = tS_.Process(targ);
        dlyL_.SetDelay(t); dlyR_.SetDelay(t);
        float dl = dlyL_.Read(), dr = dlyR_.Read();
        float fbAmt  = clampf(p2, 0.f, 0.90f);
        float tone_a = map_lin01(fbAmt, 0.10f, 0.35f);
        onepole_lp(dl, dr, tone_a, fb_lpL_, fb_lpR_);
        dlyL_.Write(dryL + fb_lpL_ * fbAmt);
        dlyR_.Write(dryR + fb_lpR_ * fbAmt);
        wetL = dl; wetR = dr;
      } break;

      case DelayMode::MultiTap: { // p2 = tap spread, p3 = feedback
        float base_ms = map_exp01(p1, 60.f, 900.f);
        float targ    = clampf(base_ms * 0.001f * sr_, 10.f, kMaxMulti);
        float baseS   = tS_.Process(targ);

        // fb_lpL_/R_ hold the previous sample's longest tap for feedback.
        float fb = clampf(p3, 0.f, 0.85f);
        dlyL_.SetDelay(10.f); dlyR_.SetDelay(10.f);
        dlyL_.Write(dryL + fb_lpL_ * fb);
        dlyR_.Write(dryR + fb_lpR_ * fb);

        float taps[3] = { 0.5f * baseS, 1.0f * baseS, 1.5f * baseS };
        float sumL = 0.f, sumR = 0.f, lastL = 0.f, lastR = 0.f;
        for (int i = 0; i < 3; i++) {
          float d = clampf(taps[i], 10.f, kMax);
          dlyL_.SetDelay(d); dlyR_.SetDelay(d);
          float xL = dlyL_.Read(), xR = dlyR_.Read();
          float pan = (i - 1) * clampf(p2, 0.f, 1.f);
          float g   = clampf(1.f - 0.2f * i, 0.5f, 1.f);
          float l   = (pan <= 0.f) ? 1.f : (1.f - pan);
          float r   = (pan >= 0.f) ? 1.f : (1.f + pan);
          sumL += xL * g * l; sumR += xR * g * r;
          lastL = xL; lastR = xR; // longest tap (i == 2)
        }
        fb_lpL_ = lastL; fb_lpR_ = lastR;
        wetL = sumL; wetR = sumR;
      } break;

      case DelayMode::EchoVerb: { // p3 = reverb blend
        float targ = tap_active
            ? clampf(tap_samps, 10.f, kMax)
            : clampf(map_exp01(p1, 30.f, 900.f) * 0.001f * sr_, 10.f, kMax);
        float t = tS_.Process(targ);
        dlyL_.SetDelay(t); dlyR_.SetDelay(t);
        float dl = dlyL_.Read(), dr = dlyR_.Read();
        float fb     = clampf(p2, 0.f, 0.90f);
        float tone_a = map_lin01(p2, 0.10f, 0.35f);
        onepole_lp(dl, dr, tone_a, fb_lpL_, fb_lpR_);
        dlyL_.Write(dryL + fb_lpL_ * fb);
        dlyR_.Write(dryR + fb_lpR_ * fb);

        float rev01 = clampf(p3, 0.f, 1.f);
        float vL, vR;
        verb_.SetFeedback(0.88f);
        verb_.SetLpFreq(map_lin01(1.f - rev01, 5000.f, 14000.f));
        verb_.Process(dl * map_lin01(rev01, 0.0f, 0.60f),
                      dr * map_lin01(rev01, 0.0f, 0.60f), &vL, &vR);
        wetL = dl + vL; wetR = dr + vR;
      } break;
    }
  }

 private:
  // v1 used 95990 as the ping/tape ceiling and 63990 for multitap base.
  static constexpr float kMax      = 95990.f;
  static constexpr float kMaxMulti = 63990.f;

  // No in-class initializers — must stay trivially constructible for SDRAM.
  float sr_;
  daisysp::DelayLine<float, 96000> dlyL_, dlyR_;
  daisysp::ReverbSc verb_;           // EchoVerb only
  float tape_ph_, fb_lpL_, fb_lpR_;
  SmoothedParam tS_;
};

} // namespace mfx
