#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "spillway/kernels.h"

using namespace spillway;
using Catch::Approx;

namespace {

// Numerically integrate a radial kernel over the 2D disc:
// integral_0^h W(r) * 2*pi*r dr, using the midpoint rule.
template <class Fn>
double integrateRadial(Fn&& w, float h, int samples = 20000) {
    const double dr = h / samples;
    double sum = 0.0;
    for (int k = 0; k < samples; ++k) {
        const double r = (k + 0.5) * dr;
        sum += w(static_cast<float>(r)) * 2.0 * std::numbers::pi * r * dr;
    }
    return sum;
}

} // namespace

TEST_CASE("Kernels integrate to 1 over their 2D support", "[kernels]") {
    for (float h : {0.2f, 0.35f, 1.0f, 2.5f}) {
        const SmoothingKernels k(h);
        CHECK(integrateRadial([&](float r) { return k.spikyPow2(r); }, h) == Approx(1.0).epsilon(1e-3));
        CHECK(integrateRadial([&](float r) { return k.spikyPow3(r); }, h) == Approx(1.0).epsilon(1e-3));
        CHECK(integrateRadial([&](float r) { return k.poly6(r * r); }, h) == Approx(1.0).epsilon(1e-3));
    }
}

TEST_CASE("Kernels are zero at and beyond the support radius", "[kernels]") {
    const float h = 0.35f;
    const SmoothingKernels k(h);
    for (float r : {h, h * 1.0001f, h * 2.0f, 100.0f}) {
        CHECK(k.spikyPow2(r) == 0.0f);
        CHECK(k.spikyPow3(r) == 0.0f);
        CHECK(k.poly6(r * r) == 0.0f);
        CHECK(k.spikyPow2Derivative(r) == 0.0f);
        CHECK(k.spikyPow3Derivative(r) == 0.0f);
    }
}

TEST_CASE("Kernels are positive and decreasing inside the support", "[kernels]") {
    const SmoothingKernels k(0.35f);
    float prev2 = k.spikyPow2(0.0f), prev3 = k.spikyPow3(0.0f), prev6 = k.poly6(0.0f);
    for (float r = 0.01f; r < 0.35f; r += 0.01f) {
        CHECK(k.spikyPow2(r) > 0.0f);
        CHECK(k.spikyPow2(r) < prev2);
        CHECK(k.spikyPow3(r) < prev3);
        CHECK(k.poly6(r * r) < prev6);
        prev2 = k.spikyPow2(r);
        prev3 = k.spikyPow3(r);
        prev6 = k.poly6(r * r);
    }
}

TEST_CASE("Kernel derivatives match finite differences", "[kernels]") {
    const SmoothingKernels k(0.35f);
    const float eps = 1e-3f;
    for (float r = 0.02f; r < 0.33f; r += 0.03f) {
        const float fd2 = (k.spikyPow2(r + eps) - k.spikyPow2(r - eps)) / (2 * eps);
        const float fd3 = (k.spikyPow3(r + eps) - k.spikyPow3(r - eps)) / (2 * eps);
        CHECK(k.spikyPow2Derivative(r) == Approx(fd2).epsilon(1e-2));
        CHECK(k.spikyPow3Derivative(r) == Approx(fd3).epsilon(1e-2));
    }
}
