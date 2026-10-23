/**
 * osc_host — nam/osc_layer.h on the desktop, the same code as the firmware.
 *
 *   osc_host <params.f32> <u.f32> <cut.f32> <res.f32> <out.f32>
 *
 * params: osc::kParams float32 (tools/osc_layer.py); u: the network's audio input in its
 * units; cut, res: V/OCT and Res CV volts per sample; out: what the layer adds, network units.
 * Build, from tools/ (gitignored):  c++ -std=c++17 -O2 -I.. -o osc_host osc_host.cpp
 */
#include "nam/osc_layer.h"

#include <cstdio>
#include <vector>

static std::vector<float> Read(const char* path)
{
    std::vector<float> v;
    FILE*              f = fopen(path, "rb");
    if(!f)
        return v;
    fseek(f, 0, SEEK_END);
    v.resize((size_t)ftell(f) / 4);
    fseek(f, 0, SEEK_SET);
    if(fread(v.data(), 4, v.size(), f) != v.size())
        v.clear();
    fclose(f);
    return v;
}

int main(int argc, char** argv)
{
    if(argc != 6)
    {
        fprintf(stderr, "usage: %s params u cut res out\n", argv[0]);
        return 2;
    }
    const auto pf = Read(argv[1]), u = Read(argv[2]), cut = Read(argv[3]), res = Read(argv[4]);
    if((int)pf.size() != osc::kParams || u.empty() || cut.size() != u.size() || res.size() != u.size())
    {
        fprintf(stderr, "bad inputs\n");
        return 1;
    }
    osc::Params p;
    osc::FromFloats(pf.data(), p);
    static osc::Layer layer;
    layer.Init(p, 48000.f);
    layer.Reset(12345);
    std::vector<float> y(u.size());
    for(size_t i = 0; i < u.size(); i++)
        y[i] = layer.Process(u[i], cut[i], res[i]);
    FILE* f = fopen(argv[5], "wb");
    fwrite(y.data(), 4, y.size(), f);
    fclose(f);
    return 0;
}
