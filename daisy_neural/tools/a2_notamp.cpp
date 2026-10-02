/**
 * a2_notamp — one not-amp rendered through the firmware's own processor
 * (src/notamp_dsp.h) and the real engine, block by block, as the module runs it.
 * The output is the raw network output, before the level table.
 *
 *   a2_notamp <wA.f32> <wB.f32|-> <in.f32> <out.f32> <kind> <lo> <hi> <log 0|1>
 *             <layer> <fixed> <u0> <u1>
 *
 * kind: sine slope freeze morph fbgain fbpitch rate. The steer goes from u0 to u1
 * across the clip (equal for a fixed setting). tools/notamp_design.py uses this to
 * measure the level tables, and to check it matches tools/a2_explore.cpp, the
 * harness the not-amps were chosen with.
 *
 * Build:  c++ -std=c++17 -O2 -I.. -I../src -o a2_notamp a2_notamp.cpp
 */
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#include "notamp_dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace nam_a2_daisy;

static bool Read(const char* p, std::vector<float>& v)
{
    FILE* f = fopen(p, "rb");
    if(!f)
        return false;
    fseek(f, 0, SEEK_END);
    v.resize((size_t)ftell(f) / 4);
    fseek(f, 0, SEEK_SET);
    const size_t g = fread(v.data(), 4, v.size(), f);
    fclose(f);
    return g == v.size();
}

int main(int argc, char** argv)
{
    if(argc != 13)
    {
        fprintf(stderr, "usage: a2_notamp wA wB|- in out kind lo hi log layer fixed u0 u1\n");
        return 2;
    }
    std::vector<float> A, B, in;
    if(!Read(argv[1], A) || !Read(argv[3], in) || (strcmp(argv[2], "-") && !Read(argv[2], B)))
    {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    const char* k = argv[5];
    notamps::Def d{};
    d.name = "host";
    d.kind = !strcmp(k, "sine")    ? notamps::Kind::Sine
           : !strcmp(k, "slope")   ? notamps::Kind::Slope
           : !strcmp(k, "freeze")  ? notamps::Kind::Freeze
           : !strcmp(k, "morph")   ? notamps::Kind::Morph
           : !strcmp(k, "fbgain")  ? notamps::Kind::FbGain
           : !strcmp(k, "fbpitch") ? notamps::Kind::FbPitch
           : !strcmp(k, "rate")    ? notamps::Kind::Rate
                                   : (notamps::Kind)255;
    if((int)d.kind == 255)
    {
        fprintf(stderr, "unknown kind %s\n", k);
        return 2;
    }
    d.lo = (float)atof(argv[6]);
    d.hi = (float)atof(argv[7]);
    d.log_map = atoi(argv[8]) != 0;
    d.layer = (uint8_t)atoi(argv[9]);
    d.fixed = (float)atof(argv[10]);
    for(float& g : d.gain_db)
        g = 0.f;
    const float u0 = (float)atof(argv[11]), u1 = (float)atof(argv[12]);

    static A2Player player;
    static notamps::Processor na;
    std::vector<float> w(kA2WeightCount);
    na.Begin(d, A.data(), B.empty() ? nullptr : B.data());
    if(!player.load_weights(na.InitialWeights(u0, w.data()), (size_t)kA2WeightCount))
    {
        fprintf(stderr, "weights rejected\n");
        return 1;
    }

    const size_t Bk = 48, blocks = (in.size() + Bk - 1) / Bk;
    in.resize(blocks * Bk, 0.f);
    std::vector<float> out(in.size());
    for(size_t b = 0; b < blocks; b++)
    {
        const float u = blocks > 1 ? u0 + (u1 - u0) * (float)b / (float)(blocks - 1) : u0;
        na.Apply(u, w.data(), [&](const float* nw) { load_weights(player.weights(), nw, w.size()); });
        na.Process(in.data() + b * Bk, out.data() + b * Bk,
                   [&](const float* x, float* y) { player.process_block_48(x, y); });
    }

    FILE* f = fopen(argv[4], "wb");
    fwrite(out.data(), 4, out.size(), f);
    fclose(f);
    return 0;
}
