/**
 * a2_trace — the real A2 engine on the desktop, logged for the MirthWaveNet animation
 * (eight4awish-video, slides/mirth-animations.md, animation 1).
 *
 * Runs nam/nam_a2_runtime.h with the built-in capture (the Orange TH100,
 * nam/fallback_capture.h) on an input, and writes JSON: the layer table from the
 * engine's own constants; every layer's three lanes over the whole input (activation
 * after the LeakyReLU, as min/max per `decim` samples); the skip sums and the output;
 * a full-rate excerpt around one sample; and one neuron at that sample taken apart:
 * each history value it read, each weight, the products, the bias, the sum and the
 * activation. That neuron's sum is rebuilt here from the engine's loaded weights and
 * checked against the sum the engine itself computed (trace::z), so what is drawn is
 * what the engine does.
 *
 * The logging hook is NAM_A2_TRACE in nam_a2_runtime.h; only this tool defines it, so
 * the firmware is unchanged.
 *
 *   a2_trace <input.f32> <out.json> <sample> [lane=0] [decim=400] [excerpt=480]
 *
 * input.f32: mono float32 at 48 kHz (the riff: test_signal.make_synth at peak 0.5, as
 * the not-amp tools feed it). <sample>: the sample the neuron is shown at (layer 1,
 * the first layer, whose inputs are the input itself).
 *
 * Build, from tools/ (gitignored):  c++ -std=c++17 -O2 -I.. -o a2_trace a2_trace.cpp
 */
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#define NAM_A2_TRACE

#include "nam/nam_a2_runtime.h"
#include "nam/fallback_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace nam_a2_daisy;

namespace
{

bool ReadFloats(const char* path, std::vector<float>& out)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fseek(f, 0, SEEK_END);
    const long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(bytes <= 0 || (bytes % 4) != 0)
    {
        fclose(f);
        return false;
    }
    out.resize((size_t)bytes / 4);
    const size_t got = fread(out.data(), 4, out.size(), f);
    fclose(f);
    return got == out.size();
}

void Floats(FILE* f, const float* v, size_t n, size_t stride = 1)
{
    fputc('[', f);
    for(size_t i = 0; i < n; i++)
        fprintf(f, "%s%.5g", i ? "," : "", v[i * stride]);
    fputc(']', f);
}

// min and max of each `decim` samples, as one flat [min, max, min, max, ...] list
void MinMax(FILE* f, const std::vector<float>& v, size_t decim)
{
    fputc('[', f);
    for(size_t i = 0; i + decim <= v.size(); i += decim)
    {
        const auto mm = std::minmax_element(v.begin() + i, v.begin() + i + decim);
        fprintf(f, "%s%.4g,%.4g", i ? "," : "", *mm.first, *mm.second);
    }
    fputc(']', f);
}

} // namespace

int main(int argc, char** argv)
{
    if(argc < 4)
    {
        fprintf(stderr, "usage: %s <input.f32> <out.json> <sample> [lane=0] [decim=400] [excerpt=480]\n", argv[0]);
        return 2;
    }
    std::vector<float> input;
    if(!ReadFloats(argv[1], input))
    {
        fprintf(stderr, "cannot read input: %s\n", argv[1]);
        return 1;
    }
    const long   at      = atol(argv[3]);
    const int    lane    = argc > 4 ? atoi(argv[4]) : 0;
    const size_t decim   = argc > 5 ? (size_t)atol(argv[5]) : 400;
    const long   excerpt = argc > 6 ? atol(argv[6]) : 480;

    static A2Player player;
    if(!player.load_weights(fallback::kWeights, kA2WeightCount))
    {
        fprintf(stderr, "load_weights rejected the built-in capture\n");
        return 1;
    }
    const A2SharedWeights& sw = player.weights();

    const size_t B    = kBlockSize;
    const size_t nblk = (input.size() + B - 1) / B;
    input.resize(nblk * B, 0.0f);
    const size_t N = input.size();
    if(at < 0 || (size_t)at >= N || at < excerpt)
    {
        fprintf(stderr, "sample %ld out of range\n", at);
        return 1;
    }

    // per layer and lane, the activation for every sample; the skip sums and output
    std::vector<std::vector<float>> act(kNumLayers * kChannels, std::vector<float>(N));
    std::vector<std::vector<float>> skip(kChannels, std::vector<float>(N));
    std::vector<float>              output(N);
    float nz = 0, na = 0;   // the engine's own sum and activation for the neuron shown

    for(size_t b = 0; b < nblk; b++)
    {
        player.process_block_48(input.data() + b * B, output.data() + b * B);
        for(size_t n = 0; n < B; n++)
        {
            const size_t t = b * B + n;
            for(int li = 0; li < kNumLayers; li++)
                for(int c = 0; c < kChannels; c++)
                    act[li * kChannels + c][t] = trace::a[li][n][c];
            for(int c = 0; c < kChannels; c++)
                skip[c][t] = player.hot_state().headSum[n * 3 + c];
            if((long)t == at)
            {
                nz = trace::z[0][n][lane];
                na = trace::a[0][n][lane];
            }
        }
    }

    // The neuron, layer 1, rebuilt from the engine's loaded weights. Its three inputs
    // at each tap are the input lanes, rechannel[c] times the input sample; its own
    // extra input is the raw sample through the mix-in weight.
    const int K = kKernelSizes[0], gap = kDilations[0];
    double sum = sw.layer[0].convB[lane] + sw.layer[0].mixinW[lane] * input[at];

    FILE* f = fopen(argv[2], "w");
    if(!f)
    {
        fprintf(stderr, "cannot write: %s\n", argv[2]);
        return 1;
    }
    fprintf(f, "{\"capture\":\"%s\",\"sampleRate\":48000,\"lanes\":%d,\"parameters\":%d,",
            fallback::kName, kChannels, kA2WeightCount);
    fprintf(f, "\"reach\":%d,\"headTaps\":%d,\"leakySlope\":%g,\"layers\":[", prewarm_samples(), kHeadKernel, kLeakySlope);
    for(int li = 0; li < kNumLayers; li++)
        fprintf(f, "%s{\"taps\":%d,\"gap\":%d}", li ? "," : "", kKernelSizes[li], kDilations[li]);
    fprintf(f, "],\"parts\":{\"inputLanes\":3,\"layerWeights\":%d,\"layerExtras\":%d,\"lastStep\":%d},",
            kConvWeightCount, kNumLayers * 18, kHeadKernel * 3 + 2);

    fprintf(f, "\"neuron\":{\"layer\":1,\"lane\":%d,\"sample\":%ld,\"taps\":[", lane, at);
    for(int k = 0; k < K; k++)
    {
        const long lag = (long)(K - 1 - k) * gap;
        float x[3], w[3];
        for(int c = 0; c < 3; c++)
        {
            x[c] = sw.rechannel[c] * input[at - lag];
            w[c] = sw.convW[kLayerConvOffset[0] + k * 9 + c * 3 + lane];
            sum += (double)w[c] * x[c];
        }
        fprintf(f, "%s{\"back\":%ld,\"in\":[%.6g,%.6g,%.6g],\"w\":[%.6g,%.6g,%.6g]}", k ? "," : "",
                lag, x[0], x[1], x[2], w[0], w[1], w[2]);
    }
    const double check = std::fabs(sum - nz);
    fprintf(f, "],\"sample\":%.6g,\"mixin\":%.6g,\"bias\":%.6g,\"sum\":%.6g,\"engineSum\":%.6g,\"activation\":%.6g},",
            input[at], sw.layer[0].mixinW[lane], sw.layer[0].convB[lane], sum, nz, na);
    fprintf(f, "\"inputLaneWeights\":[%.6g,%.6g,%.6g],", sw.rechannel[0], sw.rechannel[1], sw.rechannel[2]);

    // full rate, the `excerpt` samples up to and including the neuron's sample
    const long e0 = at - excerpt + 1;
    fprintf(f, "\"excerpt\":{\"start\":%ld,\"input\":", e0);
    Floats(f, input.data() + e0, (size_t)excerpt);
    fprintf(f, ",\"output\":");
    Floats(f, output.data() + e0, (size_t)excerpt);
    fprintf(f, ",\"layer1\":[");
    for(int c = 0; c < 3; c++)
    {
        if(c)
            fputc(',', f);
        Floats(f, act[c].data() + e0, (size_t)excerpt);
    }
    fprintf(f, "]},");

    // whole input, min/max per decim samples
    fprintf(f, "\"decim\":%zu,\"input\":", decim);
    MinMax(f, input, decim);
    fprintf(f, ",\"output\":");
    MinMax(f, output, decim);
    fprintf(f, ",\"skip\":[");
    for(int c = 0; c < 3; c++)
    {
        if(c)
            fputc(',', f);
        MinMax(f, skip[c], decim);
    }
    fprintf(f, "],\"act\":[");
    for(int li = 0; li < kNumLayers; li++)
    {
        fprintf(f, "%s[", li ? "," : "");
        for(int c = 0; c < 3; c++)
        {
            if(c)
                fputc(',', f);
            MinMax(f, act[li * kChannels + c], decim);
        }
        fputc(']', f);
    }
    fprintf(f, "]}\n");
    fclose(f);

    fprintf(stderr, "a2_trace: %zu samples, neuron at %ld lane %d: sum %.7g, engine %.7g (|diff| %.2g), activation %.7g\n",
            N, at, lane, sum, nz, check, na);
    if(check > 1e-4 * (1.0 + std::fabs(nz)))
    {
        fprintf(stderr, "a2_trace: the rebuilt sum does not match the engine's\n");
        return 1;
    }
    return 0;
}
