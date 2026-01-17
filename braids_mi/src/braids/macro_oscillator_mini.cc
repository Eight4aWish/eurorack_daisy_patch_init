#include "braids/macro_oscillator.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "stmlib/utils/dsp.h"

#include "braids/parameter_interpolation.h"

namespace braids {

using namespace stmlib;

static inline int32_t ClampI32(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void MacroOscillator::Init() {
  for (auto& osc : analog_oscillator_) {
    osc.Init();
  }
  lp_state_ = 0;
  previous_parameter_[0] = 0;
  previous_parameter_[1] = 0;
}

void MacroOscillator::Strike() {
  // In the original full Braids firmware, Strike excites the digital
  // oscillator. For the analog-only subset, approximate this by resetting
  // phases.
  for (auto& osc : analog_oscillator_) {
    osc.Reset();
  }
}

void MacroOscillator::Render(const uint8_t* sync_buffer, int16_t* buffer, size_t size) {
  RenderFn fn = fn_table_[static_cast<int>(shape_)];
  (this->*fn)(sync_buffer, buffer, size);
}

void MacroOscillator::RenderCSaw(const uint8_t* sync, int16_t* buffer, size_t size) {
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_aux_parameter(parameter_[1]);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_CSAW);
  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
}

void MacroOscillator::RenderMorph(const uint8_t* sync, int16_t* buffer, size_t size) {
  // Crossfade between variable saw and square.
  const int16_t parameter_2 = parameter_[1];

  analog_oscillator_[0].set_parameter(parameter_2);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_VARIABLE_SAW);

  analog_oscillator_[1].set_parameter(parameter_2);
  analog_oscillator_[1].set_pitch(pitch_);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;

  BEGIN_INTERPOLATE_PARAMETER_1
  while (size--) {
    INTERPOLATE_PARAMETER_1
    const uint16_t balance = static_cast<uint16_t>(parameter_1) << 1;
    *buffer = Mix(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderSawSquare(const uint8_t* sync, int16_t* buffer, size_t size) {
  // Crossfade between saw and square, with PWM on square.
  const int16_t parameter_2 = parameter_[1];

  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_SAW);

  analog_oscillator_[1].set_parameter(parameter_2);
  analog_oscillator_[1].set_pitch(pitch_);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;

  BEGIN_INTERPOLATE_PARAMETER_1
  while (size--) {
    INTERPOLATE_PARAMETER_1
    const uint16_t balance = static_cast<uint16_t>(parameter_1) << 1;
    *buffer = Mix(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderSub(const uint8_t* sync, int16_t* buffer, size_t size) {
  // Square or saw + sub oscillator.
  const bool is_saw = (shape_ == MACRO_OSC_SHAPE_SAW_SUB);

  const AnalogOscillatorShape base_shape = is_saw ? OSC_SHAPE_SAW : OSC_SHAPE_SQUARE;

  analog_oscillator_[0].set_parameter(is_saw ? 0 : parameter_[0]);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(base_shape);

  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[1].set_pitch(pitch_ - (128 << 7));
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    *buffer = (*buffer >> 1) + (*temp_buffer >> 1);
    buffer++;
    temp_buffer++;
  }
}

void MacroOscillator::RenderDualSync(const uint8_t* sync, int16_t* buffer, size_t size) {
  const AnalogOscillatorShape base_shape = (shape_ == MACRO_OSC_SHAPE_SQUARE_SYNC) ? OSC_SHAPE_SQUARE : OSC_SHAPE_SAW;

  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[0].set_shape(base_shape);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[1].set_shape(base_shape);
  analog_oscillator_[1].set_pitch(pitch_ + (parameter_[0] >> 2));

  analog_oscillator_[0].Render(sync, buffer, sync_buffer_, size);
  analog_oscillator_[1].Render(sync_buffer_, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;

  BEGIN_INTERPOLATE_PARAMETER_1
  while (size--) {
    INTERPOLATE_PARAMETER_1
    const uint16_t balance = static_cast<uint16_t>(parameter_1) << 1;
    *buffer = (Mix(*buffer, *temp_buffer, balance) >> 2) * 3;
    buffer++;
    temp_buffer++;
  }
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderSineTriangle(const uint8_t* sync, int16_t* buffer, size_t size) {
  int32_t attenuation_sine = 32767 - 6 * (pitch_ - (92 << 7));
  int32_t attenuation_tri = 32767 - 7 * (pitch_ - (80 << 7));
  attenuation_tri = ClampI32(attenuation_tri, 0, 32767);
  attenuation_sine = ClampI32(attenuation_sine, 0, 32767);

  const int32_t timbre = parameter_[0];

  analog_oscillator_[0].set_parameter(static_cast<int16_t>((timbre * attenuation_sine) >> 15));
  analog_oscillator_[1].set_parameter(static_cast<int16_t>((timbre * attenuation_tri) >> 15));
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[1].set_pitch(pitch_);

  analog_oscillator_[0].set_shape(OSC_SHAPE_SINE_FOLD);
  analog_oscillator_[1].set_shape(OSC_SHAPE_TRIANGLE_FOLD);

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;

  BEGIN_INTERPOLATE_PARAMETER_1
  while (size--) {
    INTERPOLATE_PARAMETER_1
    const uint16_t balance = static_cast<uint16_t>(parameter_1) << 1;
    *buffer = Mix(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }
  END_INTERPOLATE_PARAMETER_1
}

void MacroOscillator::RenderBuzz(const uint8_t* sync, int16_t* buffer, size_t size) {
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(parameter_[0]);
  analog_oscillator_[1].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[1].set_pitch(pitch_ + (parameter_[1] >> 8));

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    *buffer = (*buffer >> 1) + (*temp_buffer >> 1);
    buffer++;
    temp_buffer++;
  }
}

void MacroOscillator::ConfigureTriple(AnalogOscillatorShape shape) {
  const int16_t detune = parameter_[0] >> 4;
  const int16_t spread = parameter_[1] >> 6;

  analog_oscillator_[0].set_shape(shape);
  analog_oscillator_[1].set_shape(shape);
  analog_oscillator_[2].set_shape(shape);

  analog_oscillator_[0].set_parameter(detune);
  analog_oscillator_[1].set_parameter(detune);
  analog_oscillator_[2].set_parameter(detune);

  analog_oscillator_[0].set_pitch(pitch_ - spread);
  analog_oscillator_[1].set_pitch(pitch_);
  analog_oscillator_[2].set_pitch(pitch_ + spread);
}

void MacroOscillator::RenderTriple(const uint8_t* sync, int16_t* buffer, size_t size) {
  AnalogOscillatorShape base_shape = OSC_SHAPE_SAW;
  switch (shape_) {
    case MACRO_OSC_SHAPE_TRIPLE_SAW: base_shape = OSC_SHAPE_SAW; break;
    case MACRO_OSC_SHAPE_TRIPLE_SQUARE: base_shape = OSC_SHAPE_SQUARE; break;
    case MACRO_OSC_SHAPE_TRIPLE_TRIANGLE: base_shape = OSC_SHAPE_TRIANGLE; break;
    case MACRO_OSC_SHAPE_TRIPLE_SINE: base_shape = OSC_SHAPE_SINE; break;
    default: base_shape = OSC_SHAPE_SAW; break;
  }

  ConfigureTriple(base_shape);

  analog_oscillator_[0].Render(sync, buffer, nullptr, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, nullptr, size);
  analog_oscillator_[2].Render(sync, temp_buffer_, nullptr, size);

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    int32_t s = static_cast<int32_t>(*buffer) + static_cast<int32_t>(*temp_buffer);
    s += static_cast<int32_t>(*temp_buffer);
    *buffer = static_cast<int16_t>(ClampI32(s / 3, -32768, 32767));
    buffer++;
    temp_buffer++;
  }
}

/* static */
MacroOscillator::RenderFn MacroOscillator::fn_table_[] = {
  &MacroOscillator::RenderCSaw,
  &MacroOscillator::RenderMorph,
  &MacroOscillator::RenderSawSquare,
  &MacroOscillator::RenderSineTriangle,
  &MacroOscillator::RenderBuzz,
  &MacroOscillator::RenderSub,       // SQUARE_SUB
  &MacroOscillator::RenderSub,       // SAW_SUB
  &MacroOscillator::RenderDualSync,  // SQUARE_SYNC
  &MacroOscillator::RenderDualSync,  // SAW_SYNC
  &MacroOscillator::RenderTriple,    // TRIPLE_SAW
  &MacroOscillator::RenderTriple,    // TRIPLE_SQUARE
  &MacroOscillator::RenderTriple,    // TRIPLE_TRIANGLE
  &MacroOscillator::RenderTriple,    // TRIPLE_SINE
};

} // namespace braids
