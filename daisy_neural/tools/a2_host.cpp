/**
 * a2_host — run the real A2 engine on the desktop.
 *
 * The point of this is that it is not a reimplementation. nam_a2_runtime.h is
 * entirely portable — standard library only, no ARM intrinsics, prefetch off by
 * default — so the same code that runs on the Patch SM runs here. A numpy
 * rewrite of A2 would risk being subtly wrong, and then the quantisation study
 * would be measuring the rewrite's bugs rather than A2's tolerance.
 *
 *   a2_host <weights.f32> <input.f32> <output.f32>
 *
 * All three are headerless little-endian float32. The weights file is exactly
 * kA2WeightCount floats; input and output are mono at 48 kHz. Input is padded
 * to a whole number of 48-sample blocks.
 *
 * Build:  c++ -std=c++17 -O2 -I.. -o a2_host a2_host.cpp
 */

// No .dtcmram_bss or .sram_d2_bss on a desktop; let everything land normally.
#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA

#include "nam/nam_a2_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

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
    if(bytes < 0 || (bytes % 4) != 0)
    {
        fclose(f);
        return false;
    }
    out.resize((size_t)bytes / 4);
    const size_t got = out.empty() ? 0 : fread(out.data(), 4, out.size(), f);
    fclose(f);
    return got == out.size();
}

} // namespace

int main(int argc, char** argv)
{
    if(argc != 4)
    {
        fprintf(stderr, "usage: %s <weights.f32> <input.f32> <output.f32>\n", argv[0]);
        return 2;
    }

    std::vector<float> weights, input;
    if(!ReadFloats(argv[1], weights))
    {
        fprintf(stderr, "cannot read weights: %s\n", argv[1]);
        return 1;
    }
    if(!ReadFloats(argv[2], input))
    {
        fprintf(stderr, "cannot read input: %s\n", argv[2]);
        return 1;
    }

    if((int)weights.size() != nam_a2_daisy::kA2WeightCount)
    {
        fprintf(stderr, "weights: got %zu, expected %d\n",
                weights.size(), nam_a2_daisy::kA2WeightCount);
        return 1;
    }

    static nam_a2_daisy::A2Player player;
    if(!player.load_weights(weights.data(), weights.size()))
    {
        fprintf(stderr, "load_weights rejected the array\n");
        return 1;
    }

    const size_t block = (size_t)nam_a2_daisy::kBlockSize;
    const size_t nblk  = (input.size() + block - 1) / block;
    input.resize(nblk * block, 0.0f);

    std::vector<float> output(input.size(), 0.0f);
    for(size_t b = 0; b < nblk; b++)
        player.process_block_48(input.data() + b * block, output.data() + b * block);

    FILE* f = fopen(argv[3], "wb");
    if(!f)
    {
        fprintf(stderr, "cannot write: %s\n", argv[3]);
        return 1;
    }
    fwrite(output.data(), 4, output.size(), f);
    fclose(f);
    return 0;
}
