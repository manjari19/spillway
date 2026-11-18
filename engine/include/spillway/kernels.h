#pragma once

// SPH smoothing kernels (2D).
//
// In SPH every field quantity A at a point x is estimated as a weighted sum
// over nearby particles:
//
//     A(x) = sum_j  m_j * (A_j / rho_j) * W(|x - x_j|, h)
//
// W is the smoothing kernel and h its support radius: W(r) = 0 for r >= h, so
// only neighbours within h matter (that is what makes the spatial grid work).
// Each kernel is normalised so that its integral over the 2D disc is 1:
//
//     integral_0^h  W(r) * 2*pi*r  dr  = 1
//
// which means summing W over a uniform particle lattice with spacing s gives
// roughly 1/s^2, i.e. the particle number density. The normalisation
// constants below come from doing that integral by hand.

#include <numbers>

namespace spillway {

class SmoothingKernels {
public:
    explicit SmoothingKernels(float h)
        : h_(h), h2_(h * h) {
        constexpr float pi = std::numbers::pi_v<float>;
        // integral (h-r)^2 * 2*pi*r dr = pi*h^4/6
        spikyPow2Norm_ = 6.0f / (pi * h * h * h * h);
        // integral (h-r)^3 * 2*pi*r dr = pi*h^5/10
        spikyPow3Norm_ = 10.0f / (pi * h * h * h * h * h);
        // integral (h^2-r^2)^3 * 2*pi*r dr = pi*h^8/4
        poly6Norm_ = 4.0f / (pi * h2_ * h2_ * h2_ * h2_);
    }

    float radius() const { return h_; }
    float radiusSq() const { return h2_; }

    // "Spiky" kernel, (h - r)^2. Used for density. Its gradient does not
    // vanish at r = 0 (unlike poly6), so close particles still push each
    // other apart strongly, which prevents clumping.
    float spikyPow2(float r) const {
        if (r >= h_) return 0.0f;
        const float d = h_ - r;
        return d * d * spikyPow2Norm_;
    }
    // dW/dr. Always <= 0: weight falls off as distance grows.
    float spikyPow2Derivative(float r) const {
        if (r >= h_) return 0.0f;
        return -2.0f * (h_ - r) * spikyPow2Norm_;
    }

    // Sharper (h - r)^3 kernel for "near density" (Clavet et al. 2005).
    // It is much more peaked than spikyPow2, so the near-pressure it drives
    // only acts at very short range and acts as an anti-clustering term.
    float spikyPow3(float r) const {
        if (r >= h_) return 0.0f;
        const float d = h_ - r;
        return d * d * d * spikyPow3Norm_;
    }
    float spikyPow3Derivative(float r) const {
        if (r >= h_) return 0.0f;
        const float d = h_ - r;
        return -3.0f * d * d * spikyPow3Norm_;
    }

    // Smooth poly6 kernel (h^2 - r^2)^3 (Mueller et al. 2003), used for
    // viscosity. It takes r^2 directly so no square root is needed.
    float poly6(float r2) const {
        if (r2 >= h2_) return 0.0f;
        const float d = h2_ - r2;
        return d * d * d * poly6Norm_;
    }

private:
    float h_;
    float h2_;
    float spikyPow2Norm_;
    float spikyPow3Norm_;
    float poly6Norm_;
};

} // namespace spillway
