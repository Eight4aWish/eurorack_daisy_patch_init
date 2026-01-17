#pragma once

// Selects between the local trimmed Braids subset (lite) and the upstream
// full Braids headers from the shared deps tree.
//
// - Lite build: uses quoted includes so the project-local overrides in
//   src/braids/ are preferred.
// - Full build: uses angle-bracket includes so we don't accidentally pick up
//   project-local overrides from src/.

#ifdef BRAIDS_VARIANT_FULL
#include <braids/envelope.h>
#include <braids/macro_oscillator.h>
#else
#include "braids/envelope.h"
#include "braids/macro_oscillator.h"
#endif
