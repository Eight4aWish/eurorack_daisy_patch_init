// Host-side checks for Sorrow's drum voice pool.
//
// Nothing here is built into firmware. It is a PC compiler driving the real
// drum_voices.cpp and the real DaisySP models, because every one of these
// checks caught a bug that reading the code did not:
//
//   audible   - SyntheticBassDrum returns infinity if processed before its
//               parameters are set.
//   decay     - DaisySP's AnalogSnareDrum rings for seconds at *every* decay
//               setting, and the physical models sustain by design. Both needed
//               an output VCA before they were usable as drums.
//   soak      - the full render path, hunting for a kit that produces NaN.
//   spread    - model selection was a cliff, not a ramp: the modal models went
//               from never appearing to a third of all rolls across 0.1 of
//               wildness, leaving most of the pool unreachable.
//
// What it cannot check is CPU cost. Two desktop benchmarks called AnalogSnare
// cheap when it was the one model that hung the panel, because host libm makes
// powf/tanf far cheaper than they are on a Cortex-M7. The firmware measures
// that on the target at boot instead.
//
// License: GPL-3.0-or-later - see daisy_grids/LICENSE.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "drum_voices.h"

#include <cmath>
#include <cstdint>
#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace
{
uint32_t HostMicros()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint32_t g_rng = 0x1234567u;
float    Rnd()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return static_cast<float>(g_rng & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

constexpr float kSampleRate = 32000.0f;   // matches the firmware
int             g_failures  = 0;

void Fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    ++g_failures;
}

// Every model, every wildness: finite and actually audible.
void CheckAudible()
{
    printf("audible: every model produces finite, audible output\n");
    for(float w = 0.0f; w <= 1.001f; w += 0.125f)
        for(int roll = 0; roll < 80; ++roll)
        {
            sorrow::VoicesRoll(w, Rnd);
            for(int sl = 0; sl < 3; ++sl)
            {
                const auto slot = static_cast<sorrow::Slot>(sl);
                sorrow::VoiceTrig(slot, 0.8f);
                float peak = 0.0f;
                bool  bad  = false;
                for(int i = 0; i < static_cast<int>(kSampleRate); ++i)
                {
                    const float v = sorrow::VoiceProcess(slot);
                    if(!std::isfinite(v))
                    {
                        bad = true;
                        break;
                    }
                    peak = std::fmax(peak, std::fabs(v));
                }
                if(bad)
                    Fail("w=%.3f %s produced a non-finite sample", w,
                         sorrow::VoiceModelName(slot));
                else if(peak < 1e-3f)
                    Fail("w=%.3f %s is silent (peak %.6f)", w,
                         sorrow::VoiceModelName(slot), peak);
            }
        }
}

// A 16th note at 120 BPM is 125 ms. Anything ringing much past 400 ms smears.
void CheckDecay()
{
    printf("decay: kick/snare under 400 ms, hats under 800 ms\n");
    const int   kMax = static_cast<int>(kSampleRate * 3.0f);
    static float buf[96000];
    for(float w = 0.0f; w <= 1.001f; w += 0.25f)
        for(int roll = 0; roll < 60; ++roll)
        {
            sorrow::VoicesRoll(w, Rnd);
            for(int sl = 0; sl < 3; ++sl)
            {
                const auto slot = static_cast<sorrow::Slot>(sl);
                sorrow::VoiceTrig(slot, 0.8f);
                float peak = 0.0f;
                for(int i = 0; i < kMax; ++i)
                {
                    buf[i] = sorrow::VoiceProcess(slot);
                    peak   = std::fmax(peak, std::fabs(buf[i]));
                }
                int last = 0;
                for(int i = 0; i < kMax; ++i)
                    if(std::fabs(buf[i]) > peak * 0.01f)
                        last = i;
                // Hats are allowed to ring: an open hi-hat is a legitimate
                // sound, and capping every slot at 400 ms is what confined them
                // to clicks in the first place. Kick and snare still smear if
                // they run long.
                const float ms    = 1000.0f * last / kSampleRate;
                const float limit = (sl == 2) ? 800.0f : 400.0f;
                if(ms > limit)
                    Fail("w=%.2f %s rings %.0f ms (limit %.0f)", w,
                         sorrow::VoiceModelName(slot), ms, limit);
            }
        }
}

// The full render path from main.cpp: pan, mix, drive, saturate.
void CheckSoak()
{
    printf("soak: 400 kits through the render path without NaN\n");
    const int step_samples = static_cast<int>(kSampleRate / 8.0f);
    for(int roll = 0; roll < 400; ++roll)
    {
        const float w = (roll % 9) / 8.0f;
        sorrow::VoicesRoll(w, Rnd);
        const float kp = sorrow::VoicePan(sorrow::Slot::Kick);
        const float sp = sorrow::VoicePan(sorrow::Slot::Snare);
        const float hp = sorrow::VoicePan(sorrow::Slot::Hat);
        for(int step = 0; step < 32; ++step)
        {
            if(step % 4 == 0)
                sorrow::VoiceTrig(sorrow::Slot::Kick, 0.55f);
            if(step % 8 == 4)
                sorrow::VoiceTrig(sorrow::Slot::Snare, 0.45f);
            if(step % 2 == 0)
                sorrow::VoiceTrig(sorrow::Slot::Hat, 0.55f);
            for(int i = 0; i < step_samples; ++i)
            {
                const float v[3] = {sorrow::VoiceProcess(sorrow::Slot::Kick),
                                    sorrow::VoiceProcess(sorrow::Slot::Snare),
                                    sorrow::VoiceProcess(sorrow::Slot::Hat)};
                // Name the offending voice, not just the kit: a kit is three
                // models and reporting all three sends you hunting the wrong one.
                for(int sl = 0; sl < 3; ++sl)
                    if(!std::isfinite(v[sl]))
                    {
                        Fail("w=%.3f %s produced NaN at step %d (kit %s / %s / %s)",
                             w,
                             sorrow::VoiceModelName(static_cast<sorrow::Slot>(sl)),
                             step,
                             sorrow::VoiceModelName(sorrow::Slot::Kick),
                             sorrow::VoiceModelName(sorrow::Slot::Snare),
                             sorrow::VoiceModelName(sorrow::Slot::Hat));
                        return;
                    }
                const float l = 0.95f * v[0] * std::sqrt(1 - kp)
                                + 0.70f * v[1] * std::sqrt(1 - sp)
                                + 1.35f * v[2] * std::sqrt(1 - hp);
                const float out = (l * 3.5f) / (1.0f + std::fabs(l * 3.5f));
                if(!std::isfinite(out))
                {
                    Fail("w=%.3f mix produced NaN from finite voices", w);
                    return;
                }
            }
        }
    }
}

// Wildness must fade models in, not switch them on.
void CheckSpread()
{
    printf("spread: wildness ramps model choice rather than gating it\n");
    const char* slot_name[3] = {"kick", "snare", "hat"};
    for(float w = 0.0f; w <= 1.001f; w += 0.25f)
    {
        int count[3][8] = {};
        const int N     = 3000;
        for(int i = 0; i < N; ++i)
        {
            sorrow::VoicesRoll(w, Rnd);
            for(int s = 0; s < 3; ++s)
                count[s][sorrow::VoiceModelIndex(static_cast<sorrow::Slot>(s))]++;
        }
        printf("   w=%.2f", w);
        for(int s = 0; s < 3; ++s)
        {
            printf("  %s", slot_name[s]);
            const auto slot = static_cast<sorrow::Slot>(s);
            for(uint8_t m = 0; m < sorrow::VoiceModelCount(slot); ++m)
                printf(" %2d%%", 100 * count[s][m] / N);
        }
        printf("\n");
    }
    // At full wildness every model must be reachable.
    int count[3][8] = {};
    for(int i = 0; i < 3000; ++i)
    {
        sorrow::VoicesRoll(1.0f, Rnd);
        for(int s = 0; s < 3; ++s)
            count[s][sorrow::VoiceModelIndex(static_cast<sorrow::Slot>(s))]++;
    }
    for(int s = 0; s < 3; ++s)
    {
        const auto slot = static_cast<sorrow::Slot>(s);
        for(uint8_t m = 0; m < sorrow::VoiceModelCount(slot); ++m)
            if(count[s][m] == 0)
                Fail("%s model '%s' is unreachable even at full wildness",
                     slot_name[s], sorrow::VoiceModelNameAt(slot, m));
    }
}
} // namespace

int main()
{
    // Pass a micros function so the pool measures levels and applies its output
    // trims - otherwise the checks run on unnormalised voices and cannot see the
    // gain staging the firmware actually ships. The cost numbers that come out
    // of it are host speeds and therefore meaningless, but they are uniformly
    // tiny, so the kit budget excludes nothing and model choice is unaffected.
    sorrow::VoicesInit(kSampleRate, HostMicros);

    CheckAudible();
    CheckDecay();
    CheckSoak();
    CheckSpread();

    printf("\n%s\n", g_failures ? "FAILURES ABOVE" : "all voice checks passed");
    return g_failures ? 1 : 0;
}
