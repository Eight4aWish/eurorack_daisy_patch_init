// Declarations for the generated speech in src/speech_data.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace sorrow {

constexpr float kSpeechRateHz = 11025.0f;

// "E D M patterns"
extern const uint8_t kSpeechBankEdm[14424];
constexpr size_t kSpeechBankEdmLen = 14424;

// "Traditional patterns"
extern const uint8_t kSpeechBankTrad[15041];
constexpr size_t kSpeechBankTradLen = 15041;

} // namespace sorrow
