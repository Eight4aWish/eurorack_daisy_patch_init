#include "seed_weights.h"

#include <cmath>
#include <cstdio>

namespace seedweights
{
namespace
{

// Region boundaries in the flat 1871-weight array, from the runtime's own
// offsets: 3 rechannel, 1404 conv, 23*18 per-layer, 16*3 head, 2 tail.
struct Region
{
    size_t begin, end;
    float  sigma;
};

// Sigmas measured from a trained capture (JCM800) per region. Scale matters
// more than it looks: 23 layers compound, so a uniform sigma across the whole
// array either saturates into DC or vanishes. Halving these drops output RMS
// about 13x per halving; 1.5x explodes to peak 1429. Matched is the only
// usable setting, and it sits close to the edge.
constexpr Region kRegions[] = {
    {0, 3, 0.4304f},       // rechannel
    {3, 1407, 0.3323f},    // conv
    {1407, 1821, 0.3444f}, // per-layer
    {1821, 1869, 0.1480f}, // head — less than half the conv spread
    {1869, 1871, 0.0671f}, // tail
};

constexpr size_t kConvBegin = 3;
constexpr size_t kConvEnd   = 1407;

/** xorshift32. Small, fast, and the point here is reproducibility rather than
 *  statistical quality — the same seed must give the same network on any unit,
 *  forever, which rules out anything from the standard library. */
struct Rng
{
    uint32_t s;

    explicit Rng(uint32_t seed)
    : s(seed ? seed : 0x9E3779B9u)  // 0 is a fixed point for xorshift
    {
        for(int i = 0; i < 8; i++)  // scramble short seeds apart
            Next();
    }

    uint32_t Next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    float Uniform()  // (0,1), never 0 — logf() below would not forgive it
    {
        return ((Next() >> 8) + 1u) * (1.0f / 16777217.0f);
    }

    float Gauss()
    {
        // Box-Muller. The spare value is discarded rather than cached; this
        // runs 1871 times while the output is muted, so it is not worth the
        // state.
        const float u1 = Uniform();
        const float u2 = Uniform();
        return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
    }
};

} // namespace

void Generate(uint32_t seed, float tilt, float* dst, size_t count)
{
    if(!dst || count == 0)
        return;

    Rng rng(seed);

    for(const Region& r : kRegions)
    {
        for(size_t i = r.begin; i < r.end && i < count; i++)
            dst[i] = rng.Gauss() * r.sigma;
    }
    // Anything past the known regions (there should be none) is left at zero
    // rather than random, so an unexpected count cannot inject garbage.
    for(size_t i = 1871; i < count; i++)
        dst[i] = 0.0f;

    if(tilt > 0.0f)
    {
        // Brightness. The conv region is 156 taps of 9 floats — the runtime
        // lays out each tap as a 3x3 channel matrix. Alternating the sign of
        // successive TAPS turns each dilated convolution from an average into
        // a difference, and a difference is a high pass.
        //
        // The head was the obvious candidate and it is the wrong one: it is a
        // linear output stage, so tilting it changes level and leaves the
        // spectrum alone. The darkness is the conv stack.
        const float f = 1.0f - 2.0f * tilt;
        for(size_t i = kConvBegin; i < kConvEnd && i < count; i++)
        {
            if(((i - kConvBegin) / 9) % 2)
                dst[i] *= f;
        }
    }
}

void Name(uint32_t seed, char* dst, size_t len)
{
    snprintf(dst, len, "SEED %lu", (unsigned long)seed);
}

} // namespace seedweights
