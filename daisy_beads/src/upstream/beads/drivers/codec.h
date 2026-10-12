// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Wish (daisy_beads/): stands in for Beads' beads/drivers/codec.h, which drives
// the STM32H743's SAI through the ST HAL. The only part of it the portable code
// uses is Codec::Frame, the 32-bit stereo sample that gain_control.cc reads, so
// that is all this declares. The Alchemy Lab's codec is libDaisy's.
//
// Shadows the original through the include order in daisy_beads/Makefile.

#ifndef BEADS_DRIVERS_CODEC_H_
#define BEADS_DRIVERS_CODEC_H_

#include "stmlib/stmlib.h"

namespace beads {

class Codec {
 public:
  struct Frame {
    int32_t l;
    int32_t r;
  } __attribute__((packed, aligned(4)));
};

}  // namespace beads

#endif  // BEADS_DRIVERS_CODEC_H_
