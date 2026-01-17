#pragma once

// Minimal settings header for the DFU/internal-flash build.
//
// This shadows the upstream Mutable Instruments Braids header to keep the
// model set small enough to fit in STM32H750 internal flash.

namespace braids {

enum MacroOscillatorShape {
  MACRO_OSC_SHAPE_CSAW = 0,
  MACRO_OSC_SHAPE_MORPH,
  MACRO_OSC_SHAPE_SAW_SQUARE,
  MACRO_OSC_SHAPE_SINE_TRIANGLE,
  MACRO_OSC_SHAPE_BUZZ,

  MACRO_OSC_SHAPE_SQUARE_SUB,
  MACRO_OSC_SHAPE_SAW_SUB,
  MACRO_OSC_SHAPE_SQUARE_SYNC,
  MACRO_OSC_SHAPE_SAW_SYNC,

  MACRO_OSC_SHAPE_TRIPLE_SAW,
  MACRO_OSC_SHAPE_TRIPLE_SQUARE,
  MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
  MACRO_OSC_SHAPE_TRIPLE_SINE,

  MACRO_OSC_SHAPE_LAST,
};

} // namespace braids
