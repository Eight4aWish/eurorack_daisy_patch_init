// Sorrow's drum voice pool.
//
// Each of the three slots (kick, snare, hat) can be played by any of several
// DaisySP models rather than one fixed voice. Rolling the dice picks a model
// per slot and randomizes its parameters; "wildness" widens both choices — how
// exotic a model may be, how far its parameters may roam, and how willing the
// three slots are to come from different families instead of matching.
//
// The pattern is deliberately untouched by any of this: a roll changes what the
// kit sounds like and never what it plays.
//
// License: GPL-3.0-or-later — see daisy_grids/LICENSE.
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst

#pragma once

#include <cstdint>

namespace sorrow {

enum class Slot : uint8_t
{
    Kick = 0,
    Snare,
    Hat,
    kCount
};

// Returns a uniform float in [0,1). Supplied by the caller so the pool shares
// the firmware's one seeded, free-running RNG rather than keeping its own.
using RandFn = float (*)();

// Free-running microsecond counter, used once at boot to measure how expensive
// each model actually is on this hardware. Supplied by the caller so this file
// stays independent of libDaisy and can be built for host-side tests.
using MicrosFn = uint32_t (*)();

// Construct every model and select a tame starting kit.
//
// If `micros` is supplied, each model is benchmarked at boot and the cost is
// used to keep a roll from selecting a kit the CPU cannot afford. Guessing
// these costs from a desktop benchmark does not work: the models that overran
// the audio budget on the M7 were the ones a host benchmark called cheap,
// because host libm makes powf/tanf far cheaper than they are on the target.
void VoicesInit(float sample_rate, MicrosFn micros = nullptr);

// How many of the slot's models are cheap enough to be selected at all. If this
// is 1, the cost budget has squeezed the pool down to the cheapest model.

// Measured cost of the slot's current model, as a fraction of one sample
// period (0.1 = a tenth of the CPU). Zero if costs were never measured.
float VoiceCost(Slot slot);

// Introspection, for the boot-time cost report over USB.
uint8_t     VoiceModelCount(Slot slot);
const char* VoiceModelNameAt(Slot slot, uint8_t model);
float       VoiceCostAt(Slot slot, uint8_t model);
float       VoiceCheapestKitCost();

// Pick a model per slot and randomize it. wildness is 0..1.
void VoicesRoll(float wildness, RandFn rnd);

void  VoiceTrig(Slot slot, float accent);
float VoiceProcess(Slot slot);

// Equal-power pan position for the slot, 0 = left, 1 = right. Rolled with the kit.
float VoicePan(Slot slot);

// Which model is currently loaded, for documentation and debugging.
const char* VoiceModelName(Slot slot);

// Index of the loaded model within the slot's pool. Used by the boot report to
// blink out which kit was rolled, so a kit that misbehaves can be identified
// on a module with no screen.
uint8_t VoiceModelIndex(Slot slot);

} // namespace sorrow
