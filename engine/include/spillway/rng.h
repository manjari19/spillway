#pragma once

#include <cstdint>

namespace spillway {

// Tiny deterministic PRNG (SplitMix64). std::uniform_real_distribution is
// implementation-defined, so we use our own to get bit-identical results on
// every compiler and platform.
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed) {}

    uint64_t next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform float in [0, 1), built from the top 24 bits.
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

private:
    uint64_t state_;
};

} // namespace spillway
