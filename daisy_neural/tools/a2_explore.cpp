/**
 * a2_explore — the real A2 engine with one not-amp transform applied, its control
 * held or swept across the clip block by block, as CV would move it.
 *
 *   a2_explore <wA.f32> <wB.f32|-> <in.f32> <out.f32> <spec> [side.f32]
 *
 * Built with NAM_A2_EXPLORE (and optionally NAM_A2_DIL_SCALE=2..4 for gap stretch),
 * which only this tool sets; see the header. Specs, where a0:a1 is the control's
 * value at the start and end of the clip (equal for a fixed setting):
 *
 *   none                     the capture as captured (with a stretch build: stretched)
 *   slope:a0:a1              leaky-ReLU slope; 0.01 is the trained network, 1 linear
 *   sine:g0:g1               activation sin(g x)/g — a wavefolder in every neuron (the
 *                            firmware's FastSin, so this is exactly what the module runs)
 *   tanh:g0:g1               activation tanh(g x)/g — soft clipping in every neuron
 *   bias:s0:s1               every bias scaled (conv, 1x1, head)
 *   mixin:s0:s1              the raw input each layer adds, scaled
 *   condrect:k0:k1           each layer's raw-input path fed |x|*k, not x
 *   conddelay:d0:d1          ... fed x delayed by d samples (log sweep)
 *   side:k0:k1               ... fed a second signal (side.f32) times k
 *   fbgain:D:g0:g1           output fed back to the input after D samples, gain g
 *   fbdelay:g:d0:d1          the same, delay swept (log): the loop's pitch
 *   rate:R                   engine run at 1/R of the sample rate, held back up
 *   morph:t0:t1              weights A + t(B - A)
 *   mutate:a0:a1             weights A + a*B (B a noise vector)
 *   freeze:L:p0:p1           layer L output held p samples (log)
 *   fold:L:t0:t1             fold inside layer L (log)
 *   off:L:c:a0:a1            offset on lane c of layer L
 *   blend:L1+L2:a0:a1        layers faded, 1 normal, 0 bypassed
 *
 * Build:  c++ -std=c++17 -O2 -DNAM_A2_EXPLORE [-DNAM_A2_DIL_SCALE=S] -I.. \
 *             -o a2_explore a2_explore.cpp
 */
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#include "nam/nam_a2_runtime.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nam_a2_daisy;

static bool Read(const char* p, std::vector<float>& v)
{
    FILE* f = fopen(p, "rb");
    if(!f)
        return false;
    fseek(f, 0, SEEK_END);
    const long b = ftell(f);
    fseek(f, 0, SEEK_SET);
    v.resize((size_t)b / 4);
    const size_t g = fread(v.data(), 4, v.size(), f);
    fclose(f);
    return g == v.size();
}

// Indices of every bias, and of each layer's raw-input (mix-in) weights, in the
// 1,871-float file — the same order load_weights() takes them in.
static void Layout(std::vector<int>& bias, std::vector<int>& mixin)
{
    int p = 3; // rechannel
    for(int li = 0; li < kNumLayers; li++)
    {
        p += kKernelSizes[li] * 9;
        for(int c = 0; c < 3; c++) bias.push_back(p++);  // conv bias
        for(int c = 0; c < 3; c++) mixin.push_back(p++); // mix-in
        p += 9;                                          // 1x1
        for(int c = 0; c < 3; c++) bias.push_back(p++);  // 1x1 bias
    }
    p += kHeadKernel * 3;
    bias.push_back(p); // head bias (the scale after it is left alone)
}

static float Lerp(float a, float b, float u) { return a + (b - a) * u; }
static float LogLerp(float a, float b, float u) { return expf(Lerp(logf(a), logf(b), u)); }

int main(int argc, char** argv)
{
    if(argc < 6)
    {
        fprintf(stderr, "usage: a2_explore <wA> <wB|-> <in> <out> <spec> [side]\n");
        return 2;
    }
    std::vector<float> A, B, in, side, out, w;
    if(!Read(argv[1], A) || !Read(argv[3], in) || (strcmp(argv[2], "-") && !Read(argv[2], B)))
    {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    if(argc > 6 && !Read(argv[6], side))
    {
        fprintf(stderr, "side read failed\n");
        return 1;
    }

    const std::string spec = argv[5];
    char op[16] = {};
    sscanf(spec.c_str(), "%15[^:]", op);
    float a = 0, b = 0, c = 0, d = 0;
    char layers[64] = {};
    const int n4 = sscanf(spec.c_str(), "%*[^:]:%f:%f:%f:%f", &a, &b, &c, &d);
    (void)n4;

    bend::Clear();
    static A2Player player;
    if(!player.load_weights(A.data(), A.size()))
    {
        fprintf(stderr, "weights rejected\n");
        return 1;
    }

    std::vector<int> biasIdx, mixinIdx;
    Layout(biasIdx, mixinIdx);
    const bool rewrites = !strcmp(op, "bias") || !strcmp(op, "mixin") || !strcmp(op, "morph")
                       || !strcmp(op, "mutate");

    const size_t Bk = 48;
    const int R = !strcmp(op, "rate") ? (a < 1 ? 1 : (int)a) : 1;
    const size_t blocks = (in.size() + Bk * R - 1) / (Bk * R);
    in.resize(blocks * Bk * R, 0.f);
    side.resize(in.size(), 0.f);
    out.assign(in.size(), 0.f);
    w = A;

    std::vector<float> cond(Bk), x(Bk), y(Bk);
    std::vector<float> fb(1 << 16, 0.f); // feedback ring (output history)
    size_t fbw = 0;
    std::vector<float> dl(1 << 16, 0.f); // input history, for conddelay
    size_t dlw = 0;

    for(size_t k = 0; k < blocks; k++)
    {
        const float u = blocks > 1 ? (float)k / (float)(blocks - 1) : 0.f;
        explore::cond = nullptr;

        // ---- weights rewritten live (no prewarm), as the firmware does
        if(rewrites)
        {
            const float t = Lerp(a, b, u);
            if(!strcmp(op, "morph"))
                for(size_t i = 0; i < A.size(); i++) w[i] = A[i] + t * (B[i] - A[i]);
            else if(!strcmp(op, "mutate"))
                for(size_t i = 0; i < A.size(); i++) w[i] = A[i] + t * B[i];
            else
            {
                w = A;
                for(int i : (!strcmp(op, "bias") ? biasIdx : mixinIdx)) w[i] = A[i] * t;
            }
            load_weights(player.weights(), w.data(), w.size());
        }
        // ---- the network itself
        else if(!strcmp(op, "slope"))
            bend::SetActivation(bend::kActSlope, Lerp(a, b, u));
        else if(!strcmp(op, "sine") || !strcmp(op, "tanh"))
            bend::SetActivation(!strcmp(op, "sine") ? bend::kActSine : bend::kActTanh, LogLerp(a, b, u));
        else if(!strcmp(op, "freeze"))
        {
            bend::active[(int)a] = true;
            bend::freezeP[(int)a] = (int)lroundf(LogLerp(b, c, u));
        }
        else if(!strcmp(op, "fold"))
        {
            bend::active[(int)a] = true;
            bend::fold[(int)a] = LogLerp(b, c, u);
        }
        else if(!strcmp(op, "off"))
        {
            bend::active[(int)a] = true;
            bend::offset[(int)a][(int)b] = Lerp(c, d, u);
        }
        else if(!strcmp(op, "blend"))
        {
            sscanf(spec.c_str(), "%*[^:]:%63[^:]:%f:%f", layers, &a, &b);
            char buf[64];
            strcpy(buf, layers);
            for(char* p = strtok(buf, "+"); p; p = strtok(nullptr, "+"))
            {
                bend::active[atoi(p)] = true;
                bend::blend[atoi(p)] = Lerp(a, b, u);
            }
        }
        else if(strcmp(op, "none") && strcmp(op, "rate") && strcmp(op, "condrect")
                && strcmp(op, "conddelay") && strcmp(op, "side") && strcmp(op, "fbgain")
                && strcmp(op, "fbdelay"))
        {
            fprintf(stderr, "unknown spec %s\n", spec.c_str());
            return 2;
        }

        // ---- one engine block: R input blocks' worth when the rate is divided
        const size_t base = k * Bk * R;
        for(size_t n = 0; n < Bk; n++)
        {
            float s = 0.f;
            for(int r = 0; r < R; r++) s += in[base + n * R + r];
            x[n] = s / (float)R;
        }

        if(!strcmp(op, "fbgain") || !strcmp(op, "fbdelay"))
        {
            const float g = !strcmp(op, "fbgain") ? Lerp(b, c, u) : a;
            int D = !strcmp(op, "fbgain") ? (int)a : (int)lroundf(LogLerp(b, c, u));
            if(D < (int)Bk) D = (int)Bk; // the loop cannot close inside one block
            for(size_t n = 0; n < Bk; n++)
                x[n] += g * fb[(fbw + fb.size() - (size_t)D + n) & (fb.size() - 1)];
        }
        if(!strcmp(op, "condrect"))
        {
            const float kk = Lerp(a, b, u);
            for(size_t n = 0; n < Bk; n++) cond[n] = fabsf(x[n]) * kk;
            explore::cond = cond.data();
        }
        else if(!strcmp(op, "conddelay"))
        {
            const int D = (int)lroundf(LogLerp(a, b, u));
            for(size_t n = 0; n < Bk; n++)
            {
                dl[(dlw + n) & (dl.size() - 1)] = x[n];
                cond[n] = dl[(dlw + n + dl.size() - (size_t)D) & (dl.size() - 1)];
            }
            dlw += Bk;
            explore::cond = cond.data();
        }
        else if(!strcmp(op, "side"))
        {
            const float kk = Lerp(a, b, u);
            for(size_t n = 0; n < Bk; n++) cond[n] = side[base + n * R] * kk;
            explore::cond = cond.data();
        }

        player.process_block_48(x.data(), y.data());

        for(size_t n = 0; n < Bk; n++)
        {
            if(!std::isfinite(y[n])) y[n] = 0.f;
            fb[(fbw + n) & (fb.size() - 1)] = y[n];
            for(int r = 0; r < R; r++) out[base + n * R + r] = y[n];
        }
        fbw += Bk;
    }

    FILE* f = fopen(argv[4], "wb");
    fwrite(out.data(), 4, out.size(), f);
    fclose(f);
    return 0;
}
