#pragma once
#include "daisy_patch_sm.h"

// Guard usage in main with MULTIFX_SCAFFOLD to avoid touching DSP when disabled.

namespace multifx {
struct UiState {
    uint8_t patch_index;
    bool    edit_focus; // false: first FX, true: second FX
};

void InitUi(daisy::patch_sm::DaisyPatchSM& patch, float sample_rate);
void UpdateUi(daisy::patch_sm::DaisyPatchSM& patch,
              float sample_rate,
              size_t block_size,
              UiState& state);
}
