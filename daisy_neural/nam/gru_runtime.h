#pragma once
/**
 * gru_runtime.h — a single-layer GRU with a dense output, the network train_gru.py
 * trains, one sample at a time.
 *
 * Portable C++: standard library only, so the code that runs on the Patch SM also runs
 * on the desktop (tools/gru_host.cpp), where it is checked against PyTorch sample for
 * sample before anything is flashed.
 *
 * The maths is PyTorch's nn.GRU exactly, gates in PyTorch's order (r, z, n):
 *
 *     r  = sigmoid(W_ir x + b_ir + W_hr h + b_hr)
 *     z  = sigmoid(W_iz x + b_iz + W_hz h + b_hz)
 *     n  = tanh(W_in x + b_in + r * (W_hn h + b_hn))
 *     h' = (1 - z) * n + z * h
 *     y  = w_d . h' + b_d
 *
 * x is the network's inputs, each scaled as in training: the audio as it is, CV 1 in
 * tens of volts, the Res dial in tens. The scales travel in the .grnb file with the
 * weights, so the firmware passes plain volts and dial positions.
 *
 * Activations: GRU_EXACT_ACTIVATIONS uses std::tanh, for the desktop check. Otherwise
 * tanh is a 2,049-point table over [-8, 8] (8 KB) with linear interpolation, error about
 * 6e-6, far below anything audible; sigmoid(x) = 0.5 + 0.5 tanh(x / 2) shares it.
 *
 * Sizes are read from the file, up to kMaxHidden units (GRU_MAX_HIDDEN, 32 unless the
 * build says otherwise; Mirth sets 24 to save DTCMRAM) and kMaxInputs inputs. The hidden
 * size must be a multiple of 4.
 *
 * Speed (2026-10-19). The first version kept PyTorch's layout and ran three separate dot
 * products per unit: every multiply-add loaded its weight AND the history value, plus loop
 * counting, about four instructions each. GRU-24 then took the Patch SM's whole CPU. So
 * Load() repacks the weights: for each unit, its three recurrent rows interleaved column
 * by column (whh[j][k][gate]), so one load of h[k] feeds all three gates, the offsets fit
 * one instruction, and the reads run straight through memory. GRU-16 and GRU-24 with two
 * or three inputs get fully unrolled kernels (StepFixed); any other size runs the same
 * arithmetic in plain loops. The r and z biases are merged, which PyTorch's equations
 * allow; n's are not, since b_hn sits inside r * (...).
 */

#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef GRU_HOT_DATA
#define GRU_HOT_DATA
#endif

namespace gru
{

#ifndef GRU_MAX_HIDDEN
#define GRU_MAX_HIDDEN 32
#endif
inline constexpr int kMaxHidden = GRU_MAX_HIDDEN;
inline constexpr int kMaxInputs = 4;

// Input kinds, as stored in the file. The firmware fills each from its own source.
enum Kind : uint8_t
{
    kAudio = 0,
    kCv    = 1,  // CV 1, volts
    kRes   = 2,  // the Res dial, 0-10
};

// The weights, repacked by Load() for speed (see the top of the file):
//   whh[(j * H + k) * 3 + g]   unit j's three recurrent rows, interleaved by column k
//   wih[(j * 3 + g) * I + i]   unit j's input weights, gate by gate
//   bias[j * 4 + 0..3]         r: b_ir + b_hr, z: b_iz + b_hz, n: b_in, n: b_hn
// with g = 0, 1, 2 for PyTorch's gates r, z, n.
struct Net
{
    int     hidden = 0;
    int     inputs = 0;
    uint8_t kind[kMaxInputs]  = {};
    float   scale[kMaxInputs] = {};
    float   whh[3 * kMaxHidden * kMaxHidden];
    float   wih[3 * kMaxHidden * kMaxInputs];
    float   bias[4 * kMaxHidden];
    float   wd[kMaxHidden];
    float   bd = 0.f;
};

// The number of floats in a network's payload, in file order: wih, whh, bih, bhh, wd, bd.
inline constexpr int WeightCount(int hidden, int inputs)
{
    return 3 * hidden * inputs + 3 * hidden * hidden + 6 * hidden + hidden + 1;
}

// Read a payload (file order: PyTorch's weight_ih 3H x I, weight_hh 3H x H, bias_ih 3H,
// bias_hh 3H, then the dense layer's H weights and bias; little-endian float32, any
// alignment) into a Net, repacking as it goes. False if the sizes are out of range.
inline bool Load(Net& n, int hidden, int inputs, const void* payload)
{
    if(hidden <= 0 || hidden > kMaxHidden || (hidden % 4) != 0 || inputs <= 0 || inputs > kMaxInputs)
        return false;
    const int      H = hidden, I = inputs;
    const uint8_t* p = static_cast<const uint8_t*>(payload);
    auto           rd = [&p]() {
        float v;
        std::memcpy(&v, p, sizeof(float));
        p += sizeof(float);
        return v;
    };
    n.hidden = H;
    n.inputs = I;
    for(int row = 0; row < 3 * H; row++)  // weight_ih: row = gate * H + unit
        for(int i = 0; i < I; i++)
            n.wih[((row % H) * 3 + row / H) * I + i] = rd();
    for(int row = 0; row < 3 * H; row++)  // weight_hh
        for(int k = 0; k < H; k++)
            n.whh[((row % H) * H + k) * 3 + row / H] = rd();
    for(int j = 0; j < 4 * H; j++)
        n.bias[j] = 0.f;
    for(int row = 0; row < 3 * H; row++)  // bias_ih: r and z merge, n's stays apart
        n.bias[(row % H) * 4 + row / H] += rd();
    for(int row = 0; row < 3 * H; row++)  // bias_hh: n's goes in its own slot
        n.bias[(row % H) * 4 + (row / H == 2 ? 3 : row / H)] += rd();
    for(int j = 0; j < H; j++)
        n.wd[j] = rd();
    n.bd = rd();
    return true;
}

#ifdef GRU_EXACT_ACTIVATIONS
inline float Tanh(float x) noexcept { return std::tanh(x); }
#else
inline constexpr int   kTanhN     = 2048;
inline constexpr float kTanhRange = 8.f;
GRU_HOT_DATA inline float tanhTab[kTanhN + 1];
inline const bool tanhTabReady = [] {
    for(int i = 0; i <= kTanhN; i++)
        tanhTab[i] = std::tanh(-kTanhRange + 2.f * kTanhRange * (float)i / (float)kTanhN);
    return true;
}();
inline float Tanh(float x) noexcept
{
    if(x <= -kTanhRange)
        return -1.f;
    if(x >= kTanhRange)
        return 1.f;
    const float p = (x + kTanhRange) * ((float)kTanhN / (2.f * kTanhRange));
    const int   i = (int)p;
    const float f = p - (float)i;
    return tanhTab[i] + f * (tanhTab[i + 1] - tanhTab[i]);
}
#endif
inline float Sigmoid(float x) noexcept { return 0.5f + 0.5f * Tanh(0.5f * x); }

// The .grnb file (tools/grnb.py writes it; the two must agree). Little-endian:
//     0  'GRNB'   4  version (u16, 1)   6  reserved   8  hidden (u16)   10  inputs (u16)
//    12  output gain (f32)   16  name (12 bytes, NUL-padded)   28  CRC32 of the payload
//    32  input kinds (4 x u8)   36  input scales (4 x f32)   52  reserved to 64
//    64  payload: WeightCount(hidden, inputs) x f32, in Load()'s order
inline constexpr int      kGrnbHeader  = 64;
inline constexpr int      kGrnbNameLen = 12;
inline constexpr uint32_t kGrnbMagic   = 0x424E5247u;  // "GRNB" read little-endian

inline uint32_t Crc32(const uint8_t* p, size_t n) noexcept
{
    uint32_t c = 0xFFFFFFFFu;
    for(size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for(int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// Parse a whole .grnb file. name gets kGrnbNameLen bytes, NUL-terminated. False, and
// nothing loaded, on a bad magic, version, size or CRC: a damaged card must not reach
// the network as weights.
inline bool Parse(const uint8_t* buf, size_t len, Net& n, float& gain, char* name)
{
    auto u16 = [&](int o) { return (uint16_t)(buf[o] | (buf[o + 1] << 8)); };
    auto u32 = [&](int o) { return (uint32_t)buf[o] | ((uint32_t)buf[o + 1] << 8) | ((uint32_t)buf[o + 2] << 16) | ((uint32_t)buf[o + 3] << 24); };
    if(len < (size_t)kGrnbHeader || u32(0) != kGrnbMagic || u16(4) != 1)
        return false;
    const int hidden = u16(8), inputs = u16(10);
    if(hidden <= 0 || hidden > kMaxHidden || (hidden % 4) != 0 || inputs <= 0 || inputs > kMaxInputs)
        return false;
    const size_t payload = sizeof(float) * (size_t)WeightCount(hidden, inputs);
    if(len != (size_t)kGrnbHeader + payload || Crc32(buf + kGrnbHeader, payload) != u32(28))
        return false;
    if(!Load(n, hidden, inputs, buf + kGrnbHeader))
        return false;
    for(int k = 0; k < kMaxInputs; k++)
    {
        n.kind[k] = buf[32 + k];
        std::memcpy(&n.scale[k], buf + 36 + 4 * k, 4);
    }
    std::memcpy(&gain, buf + 12, 4);
    std::memcpy(name, buf + 16, kGrnbNameLen);
    name[kGrnbNameLen - 1] = '\0';
    return true;
}

// One network's running history. Zero it to start from rest, as training does.
struct State
{
    float h[kMaxHidden] = {};
    void  Reset() { std::memset(h, 0, sizeof(h)); }
};

// The step itself, for H units and I inputs. With H and I known at compile time
// (StepFixed below) the loops unroll completely; with them read from the Net, the same
// code runs in plain loops.
template <int H, int I>
inline float StepBody(const Net& n, State& s, const float* x, int h_rt, int i_rt) noexcept
{
    const int hh = H > 0 ? H : h_rt, ii = I > 0 ? I : i_rt;
    float     xs[kMaxInputs];
    for(int i = 0; i < ii; i++)
        xs[i] = x[i] * n.scale[i];

    float        hn[kMaxHidden];
    const float* w  = n.whh;
    const float* wi = n.wih;
    const float* b  = n.bias;
    for(int j = 0; j < hh; j++, w += 3 * hh, wi += 3 * ii, b += 4)
    {
        // Three running sums, one per gate, fed by one load of each history value.
        float ar = b[0], az = b[1], ac = b[3];
#pragma GCC unroll 32
        for(int k = 0; k < hh; k++)
        {
            const float h = s.h[k];
            ar += w[3 * k] * h;
            az += w[3 * k + 1] * h;
            ac += w[3 * k + 2] * h;
        }
        float xr = 0.f, xz = 0.f, xc = b[2];
#pragma GCC unroll 4
        for(int i = 0; i < ii; i++)
        {
            xr += wi[i] * xs[i];
            xz += wi[ii + i] * xs[i];
            xc += wi[2 * ii + i] * xs[i];
        }
        const float rg = Sigmoid(xr + ar);
        const float zg = Sigmoid(xz + az);
        const float ng = Tanh(xc + rg * ac);
        hn[j]          = ng + zg * (s.h[j] - ng);  // (1 - z) n + z h
    }
    float y = n.bd;
#pragma GCC unroll 32
    for(int j = 0; j < hh; j++)
    {
        s.h[j] = hn[j];
        y += n.wd[j] * hn[j];
    }
    return y;
}

template <int H, int I>
inline float StepFixed(const Net& n, State& s, const float* x) noexcept
{
    return StepBody<H, I>(n, s, x, H, I);
}

// One sample: x holds the network's raw inputs (audio sample, volts, dial position) in
// the file's input order; the scales are applied here. Returns the output sample.
inline float Step(const Net& n, State& s, const float* x) noexcept
{
    switch(n.hidden * 8 + n.inputs)
    {
        case 16 * 8 + 2: return StepFixed<16, 2>(n, s, x);
        case 16 * 8 + 3: return StepFixed<16, 3>(n, s, x);
#if GRU_MAX_HIDDEN >= 24
        case 24 * 8 + 2: return StepFixed<24, 2>(n, s, x);
        case 24 * 8 + 3: return StepFixed<24, 3>(n, s, x);
#endif
        default: return StepBody<0, 0>(n, s, x, n.hidden, n.inputs);
    }
}

} // namespace gru
