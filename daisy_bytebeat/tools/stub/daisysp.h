#pragma once
namespace daisysp {
inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float fmap(float v, float lo, float hi) { return lo + v * (hi - lo); }
} // namespace daisysp
