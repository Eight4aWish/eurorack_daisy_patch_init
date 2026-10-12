// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
// Codec::Frame below is as declared in Beads' beads/drivers/codec.h: Copyright
// 2018 Fred's Lab and Mutable Instruments (Emilie Gillet, Frederic Meslin,
// Oliver Rockstedt), under the same licence.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
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
