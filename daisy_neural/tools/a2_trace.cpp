/**
 * a2_trace — the real A2 engine on the desktop, logged for the MirthWaveNet animation
 * (eight4awish-video, slides/mirth-animations.md, animation 1).
 *
 * Runs nam/nam_a2_runtime.h with a capture (the built-in Orange TH100,
 * nam/fallback_capture.h, unless an .a2nb is given) on an input, and writes JSON: the
 * layer table and sizes from the engine's own constants; the capture's 1,871
 * parameters as stored; layer 1's weights as the engine loaded them; and, at full rate
 * over a window around one sample, the input, the output, layer 1's three sums (before
 * the activation) and, for layers 1-3, each lane's activation and residual output (what
 * the next layer reads).
 *
 * The animation draws layer 1's neuron with these weights, and the build script checks
 * every sum it draws against the sums logged here. The logging hook is NAM_A2_TRACE in
 * nam_a2_runtime.h; only this tool defines it, so the firmware is unchanged.
 *
 *   a2_trace <input.f32> <out.json> <sample> [before=1920] [after=480] [capture.a2nb]
 *
 * input.f32: mono float32 at 48 kHz (the riff: test_signal.make_synth at peak 0.5, as
 * the not-amp tools feed it). The window is <before> samples up to and including
 * <sample>, and <after> more.
 *
 * Build, from tools/ (gitignored):  c++ -std=c++17 -O2 -I.. -o a2_trace a2_trace.cpp
 */
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#define NAM_A2_TRACE

#include "nam/nam_a2_runtime.h"
#include "nam/fallback_capture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nam_a2_daisy;

namespace
{

bool ReadFile(const char* path, std::vector<unsigned char>& out)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fseek(f, 0, SEEK_END);
    const long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(bytes <= 0)
    {
        fclose(f);
        return false;
    }
    out.resize((size_t)bytes);
    const size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

void Floats(FILE* f, const float* v, size_t n, size_t stride = 1)
{
    fputc('[', f);
    for(size_t i = 0; i < n; i++)
        fprintf(f, "%s%.6g", i ? "," : "", v[i * stride]);
    fputc(']', f);
}

} // namespace

int main(int argc, char** argv)
{
    if(argc < 4)
    {
        fprintf(stderr, "usage: %s <input.f32> <out.json> <sample> [before=1920] [after=480] [capture.a2nb]\n", argv[0]);
        return 2;
    }
    std::vector<unsigned char> raw;
    if(!ReadFile(argv[1], raw) || raw.size() % 4)
    {
        fprintf(stderr, "cannot read input: %s\n", argv[1]);
        return 1;
    }
    std::vector<float> input(raw.size() / 4);
    std::memcpy(input.data(), raw.data(), raw.size());

    const long at     = atol(argv[3]);
    const long before = argc > 4 ? atol(argv[4]) : 1920;
    const long after  = argc > 5 ? atol(argv[5]) : 480;

    // the capture: the built-in one, or an .a2nb's 1,871 floats after its 32-byte header
    std::vector<float> weights(fallback::kWeights, fallback::kWeights + kA2WeightCount);
    std::string        name = fallback::kName;
    if(argc > 6)
    {
        std::vector<unsigned char> file;
        const size_t               payload = (size_t)kA2WeightCount * 4;
        if(!ReadFile(argv[6], file) || file.size() < payload)
        {
            fprintf(stderr, "cannot read capture: %s\n", argv[6]);
            return 1;
        }
        std::memcpy(weights.data(), file.data() + (file.size() - payload), payload);
        name = argv[6];
    }

    static A2Player player;
    if(!player.load_weights(weights.data(), kA2WeightCount))
    {
        fprintf(stderr, "load_weights rejected the capture\n");
        return 1;
    }
    const A2SharedWeights& sw = player.weights();

    const size_t B    = kBlockSize;
    const size_t nblk = (input.size() + B - 1) / B;
    input.resize(nblk * B, 0.0f);
    const long N  = (long)input.size();
    const long w0 = at - before + 1, w1 = at + after;   // the window, inclusive
    if(w0 < 0 || w1 >= N)
    {
        fprintf(stderr, "window %ld..%ld outside the input\n", w0, w1);
        return 1;
    }
    const size_t W = (size_t)(w1 - w0 + 1);

    // over the window: layer 1's sums; layers 1-3, each lane's activation and residual
    constexpr int kShown = 3;
    std::vector<float> output(N), z1(3 * W), act(kShown * 3 * W), res(kShown * 3 * W);
    for(size_t b = 0; b < nblk; b++)
    {
        player.process_block_48(input.data() + b * B, output.data() + b * B);
        for(size_t n = 0; n < B; n++)
        {
            const long t = (long)(b * B + n);
            if(t < w0 || t > w1)
                continue;
            const size_t i = (size_t)(t - w0);
            for(int c = 0; c < 3; c++)
            {
                z1[c * W + i] = trace::z[0][n][c];
                for(int li = 0; li < kShown; li++)
                {
                    act[(li * 3 + c) * W + i] = trace::a[li][n][c];
                    res[(li * 3 + c) * W + i] = trace::r[li][n][c];
                }
            }
        }
    }

    FILE* f = fopen(argv[2], "w");
    if(!f)
    {
        fprintf(stderr, "cannot write: %s\n", argv[2]);
        return 1;
    }
    fprintf(f, "{\"capture\":\"%s\",\"sampleRate\":48000,\"lanes\":%d,\"parameters\":%d,\"reach\":%d,",
            name.c_str(), kChannels, kA2WeightCount, prewarm_samples());
    fprintf(f, "\"headTaps\":%d,\"leakySlope\":%g,\"historyBytes\":%d,\"layers\":[", kHeadKernel, kLeakySlope, kHistoryBytes);
    for(int li = 0; li < kNumLayers; li++)
        fprintf(f, "%s{\"taps\":%d,\"gap\":%d}", li ? "," : "", kKernelSizes[li], kDilations[li]);
    fprintf(f, "],\"weights\":");
    Floats(f, weights.data(), kA2WeightCount);

    // layer 1 as loaded: w[tap][in][out] (tap 0 the oldest), biases, the raw sample's own
    // weight per lane (mix-in), and the three weights that split the sample into lanes
    fprintf(f, ",\"layer1\":{\"w\":[");
    for(int k = 0; k < kKernelSizes[0]; k++)
    {
        fprintf(f, "%s[", k ? "," : "");
        for(int in = 0; in < 3; in++)
        {
            if(in)
                fputc(',', f);
            Floats(f, sw.convW + kLayerConvOffset[0] + k * 9 + in * 3, 3);
        }
        fputc(']', f);
    }
    fprintf(f, "],\"bias\":");
    Floats(f, sw.layer[0].convB, 3);
    fprintf(f, ",\"mixin\":");
    Floats(f, sw.layer[0].mixinW, 3);
    fprintf(f, ",\"split\":");
    Floats(f, sw.rechannel, 3);
    fprintf(f, "},");

    fprintf(f, "\"window\":{\"start\":%ld,\"at\":%ld,\"input\":", w0, at);
    Floats(f, input.data() + w0, W);
    fprintf(f, ",\"output\":");
    Floats(f, output.data() + w0, W);
    fprintf(f, ",\"z1\":[");
    for(int c = 0; c < 3; c++)
    {
        if(c)
            fputc(',', f);
        Floats(f, z1.data() + c * W, W);
    }
    fprintf(f, "],\"act\":[");
    for(int li = 0; li < kShown; li++)
    {
        fprintf(f, "%s[", li ? "," : "");
        for(int c = 0; c < 3; c++)
        {
            if(c)
                fputc(',', f);
            Floats(f, act.data() + (li * 3 + c) * W, W);
        }
        fputc(']', f);
    }
    fprintf(f, "],\"res\":[");
    for(int li = 0; li < kShown; li++)
    {
        fprintf(f, "%s[", li ? "," : "");
        for(int c = 0; c < 3; c++)
        {
            if(c)
                fputc(',', f);
            Floats(f, res.data() + (li * 3 + c) * W, W);
        }
        fputc(']', f);
    }
    fprintf(f, "]}}\n");
    fclose(f);
    fprintf(stderr, "a2_trace: %s, %ld samples, window %ld..%ld\n", name.c_str(), N, w0, w1);
    return 0;
}
