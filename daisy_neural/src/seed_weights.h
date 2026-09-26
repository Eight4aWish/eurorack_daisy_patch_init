/**
 * seed_weights — a capture that was never trained on anything.
 *
 * Generates A2 weights from a seed number instead of reading them off the card.
 * The network models nothing; it is a nonlinearity that has never existed, and
 * the same seed gives the same one forever, on any unit.
 *
 * Not a new idea — see the prior art note in tools/random_weights.py. Steinmetz
 * and Reiss published randomly-weighted networks as audio effects in 2020, with
 * code and a plugin. What is different here is the fixed amp-modelling
 * architecture, an MCU, and CV.
 *
 * Two things this needs that a trained capture does not, both measured:
 *
 *   A DC BLOCKER. Untrained LeakyReLU stacks are asymmetric and nothing has
 *   trained that out, so they park on a large DC offset with the audio riding
 *   on top — measured RMS 20.4 of which 20.4 was DC. The audio underneath is
 *   fine. main.cpp blocks it on the way out.
 *
 *   AUTO-GAIN. Output level varies about 50x across seeds (RMS 0.006 to 0.304,
 *   measured on this generator), so without normalising, turning the seed knob
 *   is mostly a volume control.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace seedweights
{

/** Fill dst with weights for this seed.
 *
 *  tilt 0..1 sets brightness. Random dilated convolutions average, and
 *  averaging is a low pass, so untrained networks are dark by default —
 *  measured centroids of 305 to 2919 Hz against a trained capture's 4320.
 *  Alternating the sign of successive taps turns each convolution from an
 *  average into a difference, which is a high pass, and moves the centroid
 *  roughly 1.7x to 2.3x.
 *
 *  Not real-time safe: generates the whole array. Main loop only, muted.
 */
void Generate(uint32_t seed, float tilt, float* dst, size_t count);

/** Display name for a seed, e.g. "SEED 42". Writes at most len bytes. */
void Name(uint32_t seed, char* dst, size_t len);

} // namespace seedweights
