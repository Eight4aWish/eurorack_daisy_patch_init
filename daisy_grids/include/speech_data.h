// Declarations for the generated speech in src/speech_data.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace sorrow {

constexpr float kSpeechRateHz = 11025.0f;

// "Original patterns"
extern const uint8_t kSpeechBankOriginal[13988];
constexpr size_t kSpeechBankOriginalLen = 13988;

// "Club patterns"
extern const uint8_t kSpeechBankClub[11397];
constexpr size_t kSpeechBankClubLen = 11397;

// "Traditional patterns"
extern const uint8_t kSpeechBankTrad[15041];
constexpr size_t kSpeechBankTradLen = 15041;

// "Latin patterns"
extern const uint8_t kSpeechBankLatin[12572];
constexpr size_t kSpeechBankLatinLen = 12572;

// "User patterns"
extern const uint8_t kSpeechBankUser[11931];
constexpr size_t kSpeechBankUserLen = 11931;

} // namespace sorrow
