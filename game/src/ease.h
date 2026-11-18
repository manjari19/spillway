#pragma once

#include <raylib.h>

#include <algorithm>
#include <cmath>

// Easing curves and helpers. Every animated value in the UI goes through
// one of these so nothing snaps.
namespace ease {

inline float clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }

inline float outCubic(float t) {
    const float u = 1.0f - clamp01(t);
    return 1.0f - u * u * u;
}
inline float inOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
// Overshoots slightly past 1 and settles back: used for stars popping in.
inline float outBack(float t) {
    constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float u = clamp01(t) - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// Frame-rate independent exponential approach toward a target: the
// remaining distance shrinks by a constant factor per second. Feels like an
// ease-out and never overshoots.
inline float approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - std::exp(-rate * dt));
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline Color mix(Color a, Color b, float t) {
    t = clamp01(t);
    auto m = [t](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
inline Color alpha(Color c, float a) {
    return {c.r, c.g, c.b, static_cast<unsigned char>(std::lround(static_cast<float>(c.a) * clamp01(a)))};
}

} // namespace ease
