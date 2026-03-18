#pragma once

// Minimal MacroOscillator implementation for DFU/internal-flash builds.
//
// Provides the same basic interface used by the app (shape/pitch/parameters,
// Strike(), Render()), but only includes the analog-style Braids algorithms.

#include <cstddef>
#include <cstdint>

#include "braids/analog_oscillator.h"
#include "braids/settings.h"

namespace braids {

class MacroOscillator {
 public:
  MacroOscillator() = default;
  ~MacroOscillator() = default;

  void Init();

  inline void set_shape(MacroOscillatorShape shape) {
    if (shape != shape_) {
      Strike();
    }
    shape_ = shape;
  }

  inline void set_pitch(int16_t pitch) { pitch_ = pitch; }

  inline void set_parameters(int16_t parameter_1, int16_t parameter_2) {
    parameter_[0] = parameter_1;
    parameter_[1] = parameter_2;
  }

  void Strike();

  void Render(const uint8_t* sync_buffer, int16_t* buffer, size_t size);

 private:
  void RenderCSaw(const uint8_t*, int16_t*, size_t);
  void RenderMorph(const uint8_t*, int16_t*, size_t);
  void RenderSawSquare(const uint8_t*, int16_t*, size_t);
  void RenderSub(const uint8_t*, int16_t*, size_t);
  void RenderDualSync(const uint8_t*, int16_t*, size_t);
  void RenderSineTriangle(const uint8_t*, int16_t*, size_t);
  void RenderBuzz(const uint8_t*, int16_t*, size_t);
  void RenderTriple(const uint8_t*, int16_t*, size_t);
  void ConfigureTriple(AnalogOscillatorShape shape);

  using RenderFn = void (MacroOscillator::*)(const uint8_t*, int16_t*, size_t);

  int16_t parameter_[2] = {0, 0};
  int16_t previous_parameter_[2] = {0, 0};
  int16_t pitch_ = (60 << 7);

  uint8_t sync_buffer_[24] = {0};
  int16_t temp_buffer_[24] = {0};

  int32_t lp_state_ = 0;

  AnalogOscillator analog_oscillator_[3];

  MacroOscillatorShape shape_ = MACRO_OSC_SHAPE_CSAW;

  static RenderFn fn_table_[];
};

} // namespace braids
