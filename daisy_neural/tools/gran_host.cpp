// gran_host.cpp — nam/gran_layer.h on the desktop, for tools/gran_check.py.
//
//   c++ -std=c++17 -O2 -I. tools/gran_host.cpp -o tools/gran_host
//   tools/gran_host <version-2 .grnb> <in.bin> <out.bin> [seed]
//
// in.bin:  u32 n, then n f32 samples, then one f32 cutoff (volts) and one f32 Res per
//          block of 48 (ceil(n / 48) of each).
// out.bin: n f32 of the layer's output, then n f32 of the input through DelayDry(),
//          both kDelay late.
// Blocks of 48 exactly as Mirth runs them: every sample through Process(), then one
// Advance() with that block's cutoff and Res.

#include "nam/gran_layer.h"
#include "nam/gru_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static std::vector<uint8_t> ReadAll(const char* path)
{
    std::vector<uint8_t> v;
    FILE*                f = std::fopen(path, "rb");
    if(!f)
        return v;
    std::fseek(f, 0, SEEK_END);
    v.resize((size_t)std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    if(std::fread(v.data(), 1, v.size(), f) != v.size())
        v.clear();
    std::fclose(f);
    return v;
}

static uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static gran::Engine engine;  // ~80 KB of tables and rings

int main(int argc, char** argv)
{
    if(argc < 4)
    {
        std::fprintf(stderr, "usage: gran_host file.grnb in.bin out.bin [seed]\n");
        return 2;
    }
    const std::vector<uint8_t> g = ReadAll(argv[1]);
    if(g.size() < 64 || U32(g.data()) != gru::kGrnbMagic || (g[4] | (g[5] << 8)) != 2)
    {
        std::fprintf(stderr, "%s: not a version-2 .grnb\n", argv[1]);
        return 1;
    }
    const int    hidden = g[8] | (g[9] << 8), inputs = g[10] | (g[11] << 8);
    const size_t at     = 64 + sizeof(float) * (size_t)gru::WeightCount(hidden, inputs);
    const size_t lsize  = U32(g.data() + 52);
    if(at + lsize != g.size() || gru::Crc32(g.data() + at, lsize) != U32(g.data() + 56))
    {
        std::fprintf(stderr, "%s: layer size or CRC mismatch\n", argv[1]);
        return 1;
    }
    gran::Layer layer;
    if(!gran::Parse(g.data() + at, lsize, layer))
    {
        std::fprintf(stderr, "%s: the layer section does not parse\n", argv[1]);
        return 1;
    }

    const std::vector<uint8_t> in = ReadAll(argv[2]);
    if(in.size() < 4)
        return 1;
    const uint32_t n  = U32(in.data());
    const uint32_t nb = (n + gran::kBlock - 1) / gran::kBlock;
    if(in.size() != 4 + 4ull * n + 8ull * nb)
    {
        std::fprintf(stderr, "%s: wrong size\n", argv[2]);
        return 1;
    }
    const float* x   = reinterpret_cast<const float*>(in.data() + 4);
    const float* cut = x + n;
    const float* res = cut + nb;

    engine.Init();
    engine.Reset(argc > 4 ? (uint32_t)std::strtoul(argv[4], nullptr, 0) : 0x2545F491u);
    std::vector<float> y(n), dry(n);
    for(uint32_t b = 0; b < nb; b++)
    {
        const uint32_t end = (b + 1) * gran::kBlock < n ? (b + 1) * gran::kBlock : n;
        for(uint32_t t = b * gran::kBlock; t < end; t++)
        {
            y[t]   = engine.Process(x[t]);
            dry[t] = engine.DelayDry(x[t]);
        }
        engine.Advance(layer, cut[b], res[b]);
    }
    FILE* f = std::fopen(argv[3], "wb");
    if(!f || std::fwrite(y.data(), 4, n, f) != n || std::fwrite(dry.data(), 4, n, f) != n)
        return 1;
    std::fclose(f);
    std::printf("%u samples, %d bank(s), %d bands, %d takes, grains of %d frames\n", n, layer.banks, layer.bands,
                layer.takes, layer.grain);
    return 0;
}
