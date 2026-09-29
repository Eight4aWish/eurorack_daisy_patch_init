/**
 * a2_host_steer — the real A2 engine with one not-amp control swept across a
 * clip, changed every 48-sample block while playing, as CV would move it.
 *
 *   a2_host_steer <wA.f32> <wB.f32> <in.f32> <out.f32> <spec>
 *
 * spec (the two numbers are the control's value at the start and the end):
 *   morph:t0:t1               weights = A + t(B - A), rewritten live
 *   mutate:a0:a1              weights = A + a*B (B = a noise vector), live
 *   freeze:L:P0:P1            hold length in samples, log ramp
 *   off:L:c:a0:a1             offset on lane c
 *   fold:L:t0:t1              fold threshold on all lanes, log ramp
 *   blend:L1+L2+...:a0:a1     layer blend, 1 = normal, 0 = layer bypassed
 * Give both ends the same value for a fixed setting.
 *
 * Weights are loaded (with prewarm) once at the start and then rewritten with
 * nam_a2_daisy::load_weights() every block with no prewarm — the same thing the
 * firmware does when a morph or mutation control moves.
 *
 * Build:  c++ -std=c++17 -O2 -I.. -o a2_host_steer a2_host_steer.cpp
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

int main(int argc, char** argv)
{
    if(argc != 6)
    {
        fprintf(stderr, "usage: a2_host_steer <wA> <wB> <in> <out> <spec>\n");
        return 2;
    }
    std::vector<float> A, B, in, out, w;
    if(!Read(argv[1], A) || !Read(argv[2], B) || !Read(argv[3], in))
    {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    const std::string spec = argv[5];
    char op[16] = {};
    sscanf(spec.c_str(), "%15[^:]", op);

    bend::Clear();
    static A2Player player;
    if(!player.load_weights(A.data(), A.size()))
    {
        fprintf(stderr, "weights rejected\n");
        return 1;
    }

    const size_t Bk = 48, blocks = (in.size() + Bk - 1) / Bk;
    in.resize(blocks * Bk, 0.f);
    out.resize(blocks * Bk);
    w = A;
    bool first = true;
    float a = 0, b = 0, c = 0, d = 0;
    char layers[64] = {};

    for(size_t k = 0; k < blocks; k++)
    {
        const float u = blocks > 1 ? (float)k / (float)(blocks - 1) : 0.f;
        if(!strcmp(op, "morph") || !strcmp(op, "mutate"))
        {
            sscanf(spec.c_str(), "%*[^:]:%f:%f", &a, &b);
            const float t = a + (b - a) * u;
            const bool morph = !strcmp(op, "morph");
            for(size_t i = 0; i < A.size(); i++)
                w[i] = morph ? A[i] + t * (B[i] - A[i]) : A[i] + t * B[i];
            if(first)
            {
                player.load_weights(w.data(), w.size());
                first = false;
            }
            else
                load_weights(player.weights(), w.data(), w.size());
        }
        else if(!strcmp(op, "freeze"))
        {
            sscanf(spec.c_str(), "%*[^:]:%f:%f:%f", &a, &b, &c);
            const int L = (int)a;
            bend::active[L]  = true;
            bend::freezeP[L] = (int)lroundf(expf(logf(b) + (logf(c) - logf(b)) * u));
        }
        else if(!strcmp(op, "off"))
        {
            sscanf(spec.c_str(), "%*[^:]:%f:%f:%f:%f", &a, &b, &c, &d);
            bend::active[(int)a]              = true;
            bend::offset[(int)a][(int)b]      = c + (d - c) * u;
        }
        else if(!strcmp(op, "fold"))
        {
            sscanf(spec.c_str(), "%*[^:]:%f:%f:%f", &a, &b, &c);
            bend::active[(int)a] = true;
            bend::fold[(int)a]   = expf(logf(b) + (logf(c) - logf(b)) * u);
        }
        else if(!strcmp(op, "blend"))
        {
            sscanf(spec.c_str(), "%*[^:]:%63[^:]:%f:%f", layers, &a, &b);
            char buf[64];
            strcpy(buf, layers);
            for(char* p = strtok(buf, "+"); p; p = strtok(nullptr, "+"))
            {
                bend::active[atoi(p)] = true;
                bend::blend[atoi(p)]  = a + (b - a) * u;
            }
        }
        else
        {
            fprintf(stderr, "unknown spec %s\n", spec.c_str());
            return 2;
        }
        player.process_block_48(in.data() + k * Bk, out.data() + k * Bk);
    }

    FILE* f = fopen(argv[4], "wb");
    fwrite(out.data(), 4, out.size(), f);
    fclose(f);
    return 0;
}
