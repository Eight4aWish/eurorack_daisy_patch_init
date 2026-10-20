/**
 * gru_host — run the Daisy's GRU engine on the desktop.
 *
 * As with a2_host, this is not a reimplementation: nam/gru_runtime.h is portable, so the
 * code that runs on the Patch SM runs here, and tools/gru_check.py compares it with
 * PyTorch sample for sample.
 *
 *   gru_host <net.grnb> <input.f32> <output.f32>
 *
 * input.f32 is headerless little-endian float32, the network's raw inputs interleaved
 * one sample at a time in the file's input order (audio sample, CV 1 volts, Res dial).
 * output.f32 is mono float32. The state starts from rest, as in training.
 *
 * Build both, from tools/ (they are gitignored):
 *   c++ -std=c++17 -O2 -I.. -DGRU_EXACT_ACTIVATIONS -o gru_host_exact gru_host.cpp
 *   c++ -std=c++17 -O2 -I.. -o gru_host gru_host.cpp
 */

#include "nam/gru_runtime.h"

#include <cstdio>
#include <vector>

namespace
{

bool ReadAll(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fseek(f, 0, SEEK_END);
    const long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(bytes > 0 ? (size_t)bytes : 0);
    const size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return bytes >= 0 && got == out.size();
}

} // namespace

int main(int argc, char** argv)
{
    if(argc != 4)
    {
        fprintf(stderr, "usage: %s <net.grnb> <input.f32> <output.f32>\n", argv[0]);
        return 2;
    }
    std::vector<uint8_t> file, raw;
    if(!ReadAll(argv[1], file) || !ReadAll(argv[2], raw))
    {
        fprintf(stderr, "cannot read %s or %s\n", argv[1], argv[2]);
        return 1;
    }
    static gru::Net net;
    float           gain = 1.f;
    char            name[gru::kGrnbNameLen];
    // A version-2 file's layer follows the network; Parse() takes the header and network
    // only, as the firmware gives it.
    size_t len = file.size();
    if(len >= (size_t)gru::kGrnbHeader && (file[4] | (file[5] << 8)) == 2)
        len = gru::kGrnbHeader + sizeof(float) * (size_t)gru::WeightCount(file[8] | (file[9] << 8), file[10] | (file[11] << 8));
    if(len > file.size() || !gru::Parse(file.data(), len, net, gain, name))
    {
        fprintf(stderr, "%s: not a valid .grnb\n", argv[1]);
        return 1;
    }
    const size_t frame = sizeof(float) * (size_t)net.inputs;
    if(raw.size() % frame != 0)
    {
        fprintf(stderr, "%s: not a whole number of %d-input frames\n", argv[2], net.inputs);
        return 1;
    }
    const size_t       n = raw.size() / frame;
    std::vector<float> x(raw.size() / sizeof(float)), y(n);
    memcpy(x.data(), raw.data(), raw.size());
    gru::State s;
    for(size_t i = 0; i < n; i++)
        y[i] = gru::Step(net, s, &x[i * net.inputs]) * gain;
    FILE* f = fopen(argv[3], "wb");
    if(!f || fwrite(y.data(), sizeof(float), n, f) != n)
    {
        fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    fclose(f);
    fprintf(stderr, "%s: '%s' GRU-%d x %d inputs, %zu samples\n", argv[1], name, net.hidden, net.inputs, n);
    return 0;
}
