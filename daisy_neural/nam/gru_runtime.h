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
 * tanh is a 4,097-point table over [-8, 8] with linear interpolation (error about 1e-5,
 * far below anything audible), and sigmoid(x) = 0.5 + 0.5 tanh(x / 2) shares it.
 *
 * Sizes are read from the file, up to kMaxHidden units and kMaxInputs inputs. The hidden
 * size must be a multiple of 4: the inner loops are unrolled by four.
 */

#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef GRU_HOT_DATA
#define GRU_HOT_DATA
#endif

namespace gru
{

inline constexpr int kMaxHidden = 32;
inline constexpr int kMaxInputs = 4;

// Input kinds, as stored in the file. The firmware fills each from its own source.
enum Kind : uint8_t
{
    kAudio = 0,
    kCv    = 1,  // CV 1, volts
    kRes   = 2,  // the Res dial, 0-10
};

// The weights, PyTorch's layout, row-major: wih is 3H x I, whh is 3H x H.
struct Net
{
    int     hidden = 0;
    int     inputs = 0;
    uint8_t kind[kMaxInputs]  = {};
    float   scale[kMaxInputs] = {};
    float   wih[3 * kMaxHidden * kMaxInputs];
    float   whh[3 * kMaxHidden * kMaxHidden];
    float   bih[3 * kMaxHidden];
    float   bhh[3 * kMaxHidden];
    float   wd[kMaxHidden];
    float   bd = 0.f;
};

// The number of floats in a network's payload, in file order: wih, whh, bih, bhh, wd, bd.
inline constexpr int WeightCount(int hidden, int inputs)
{
    return 3 * hidden * inputs + 3 * hidden * hidden + 6 * hidden + hidden + 1;
}

// Copy a payload (file order) into a Net. False if the sizes are out of range.
inline bool Load(Net& n, int hidden, int inputs, const float* w)
{
    if(hidden <= 0 || hidden > kMaxHidden || (hidden % 4) != 0 || inputs <= 0 || inputs > kMaxInputs)
        return false;
    n.hidden = hidden;
    n.inputs = inputs;
    const int g = 3 * hidden;
    std::memcpy(n.wih, w, sizeof(float) * g * inputs);
    w += g * inputs;
    std::memcpy(n.whh, w, sizeof(float) * g * hidden);
    w += g * hidden;
    std::memcpy(n.bih, w, sizeof(float) * g);
    w += g;
    std::memcpy(n.bhh, w, sizeof(float) * g);
    w += g;
    std::memcpy(n.wd, w, sizeof(float) * hidden);
    w += hidden;
    n.bd = *w;
    return true;
}

#ifdef GRU_EXACT_ACTIVATIONS
inline float Tanh(float x) noexcept { return std::tanh(x); }
#else
inline constexpr int   kTanhN     = 4096;
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

// Dot product of n floats, n a multiple of 4.
inline float Dot4(const float* a, const float* b, int n) noexcept
{
    float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;
    for(int i = 0; i < n; i += 4)
    {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    return (s0 + s1) + (s2 + s3);
}

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
    static float w[WeightCount(kMaxHidden, kMaxInputs)];
    std::memcpy(w, buf + kGrnbHeader, payload);
    if(!Load(n, hidden, inputs, w))
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

// One sample: x holds the network's raw inputs (audio sample, volts, dial position) in
// the file's input order; the scales are applied here. Returns the output sample.
inline float Step(const Net& n, State& s, const float* x) noexcept
{
    const int H = n.hidden, I = n.inputs;
    float     xs[kMaxInputs];
    for(int k = 0; k < I; k++)
        xs[k] = x[k] * n.scale[k];

    float hn[kMaxHidden];
    for(int j = 0; j < H; j++)
    {
        const int r = j, z = H + j, c = 2 * H + j;  // the three gate rows for unit j
        float     ir = n.bih[r], iz = n.bih[z], ic = n.bih[c];
        for(int k = 0; k < I; k++)
        {
            ir += n.wih[r * I + k] * xs[k];
            iz += n.wih[z * I + k] * xs[k];
            ic += n.wih[c * I + k] * xs[k];
        }
        const float hr = Dot4(&n.whh[r * H], s.h, H) + n.bhh[r];
        const float hz = Dot4(&n.whh[z * H], s.h, H) + n.bhh[z];
        const float hc = Dot4(&n.whh[c * H], s.h, H) + n.bhh[c];
        const float rg = Sigmoid(ir + hr);
        const float zg = Sigmoid(iz + hz);
        const float ng = Tanh(ic + rg * hc);
        hn[j]          = (1.f - zg) * ng + zg * s.h[j];
    }
    std::memcpy(s.h, hn, sizeof(float) * H);
    return Dot4(n.wd, s.h, H) + n.bd;
}

} // namespace gru
