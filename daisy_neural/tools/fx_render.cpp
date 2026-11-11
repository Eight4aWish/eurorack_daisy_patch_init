// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Renders one of Mirth's FX bank effects on the Mac, exactly as the module runs it: the
// same src/fx_bank.h, 48 kHz, 48-sample blocks, the knobs held where they are set.
// Raw float32 stereo interleaved in and out (tools/fx_listen.py does the files):
//
//   fx_render <effect 0-7> <knob2> <knob3> <knob4> < in.f32 > wet.f32
//
// Writes the wet signal only; the mix is the host's. With FX_TIMING=1 in the environment it
// prints the nanoseconds a sample to stderr, a rough guide only: the module's own CPU
// readout is the measure that counts.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "fx_bank.h"

static fx::Bank bank;  // ordinary memory here; the module puts it in SDRAM

int main(int argc, char** argv)
{
    if(argc < 5)
    {
        fprintf(stderr, "usage: fx_render <effect> <k2> <k3> <k4> < in.f32 > out.f32\n");
        return 2;
    }
    const int   effect = atoi(argv[1]);
    const float p[3]   = {(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
    std::vector<float> in;
    float              buf[4096];
    size_t             got;
    while((got = fread(buf, sizeof(float), 4096, stdin)) > 0)
        in.insert(in.end(), buf, buf + got);
    const size_t frames = in.size() / 2;

    constexpr size_t kBlock = 48;
    bank.Init(48000.f, kBlock);
    bank.Reset(effect);
    const fx::Tap tap = {false, 0.f};
    float          inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];
    std::vector<float> out(frames * 2, 0.f);
    const auto t0 = std::chrono::steady_clock::now();
    for(size_t f = 0; f + kBlock <= frames; f += kBlock)
    {
        for(size_t k = 0; k < kBlock; k++)
        {
            inL[k] = in[2 * (f + k)];
            inR[k] = in[2 * (f + k) + 1];
        }
        bank.Process(inL, inR, outL, outR, kBlock, p, tap);
        for(size_t k = 0; k < kBlock; k++)
        {
            out[2 * (f + k)]     = outL[k];
            out[2 * (f + k) + 1] = outR[k];
        }
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    if(getenv("FX_TIMING"))
        fprintf(stderr, "%.1f ns/sample\n", ns / (double)frames);
    fwrite(out.data(), sizeof(float), out.size(), stdout);
    return 0;
}
