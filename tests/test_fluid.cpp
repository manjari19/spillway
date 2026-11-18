#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "spillway/simulation.h"

using namespace spillway;
using Catch::Approx;

TEST_CASE("Interior density of a rest-spaced block is close to rest density", "[fluid]") {
    FluidParams params;
    FluidSystem fluid(params, {0, 0}, {10, 10});
    fluid.spawnBlock({2, 2}, {8, 8});

    // Sample well inside the block, more than h from any edge, so the kernel
    // support is fully covered by particles.
    for (Vec2 p : {Vec2{5, 5}, Vec2{4, 6}, Vec2{6.3f, 4.1f}}) {
        CHECK(fluid.sampleDensity(p) == Approx(params.targetDensity).epsilon(0.08));
    }
}

TEST_CASE("Grid and brute-force neighbour search give the same simulation", "[fluid]") {
    // Summation order differs between the two modes, so allow float noise.
    auto run = [](NeighbourSearch mode) {
        Simulation sim(FluidParams{}, {0, 0}, {6, 6});
        sim.fluid().spawnBlock({0.5f, 0.5f}, {3.5f, 3.5f});
        sim.fluid().setNeighbourSearch(mode);
        for (int i = 0; i < 20; ++i) sim.step();
        return std::vector<Vec2>(sim.fluid().positions().begin(), sim.fluid().positions().end());
    };
    const auto a = run(NeighbourSearch::Grid);
    const auto b = run(NeighbourSearch::BruteForce);
    REQUIRE(a.size() == b.size());
    float maxErr = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) maxErr = std::max(maxErr, length(a[i] - b[i]));
    CHECK(maxErr < 1e-3f);
}

TEST_CASE("Particles stay inside the box and settle", "[fluid]") {
    Simulation sim(FluidParams{}, {0, 0}, {8, 6});
    sim.fluid().spawnBlock({0.2f, 0.2f}, {4.0f, 4.0f});
    for (int i = 0; i < 600; ++i) sim.step(); // 10 simulated seconds

    const auto& f = sim.fluid();
    float maxSpeed = 0.0f;
    double meanSpeed = 0.0;
    float maxDensity = 0.0f;
    for (size_t i = 0; i < f.size(); ++i) {
        const Vec2 p = f.positions()[i];
        REQUIRE(std::isfinite(p.x));
        REQUIRE(std::isfinite(p.y));
        REQUIRE(p.x >= 0.0f);
        REQUIRE(p.x <= 8.0f);
        REQUIRE(p.y >= 0.0f);
        REQUIRE(p.y <= 6.0f);
        maxSpeed = std::max(maxSpeed, length(f.velocities()[i]));
        meanSpeed += length(f.velocities()[i]);
        maxDensity = std::max(maxDensity, f.densities()[i]);
    }
    meanSpeed /= static_cast<double>(f.size());
    // After the dam-break sloshes out, the pool should be nearly still and
    // only mildly compressed under its own weight.
    CHECK(meanSpeed < 0.5);
    CHECK(maxSpeed < 2.0f);
    CHECK(maxDensity < 1.2f * f.params().targetDensity);
}

TEST_CASE("Same setup run twice gives bit-identical state", "[fluid][determinism]") {
    auto run = [] {
        Simulation sim(FluidParams{}, {0, 0}, {6, 6});
        sim.fluid().spawnBlock({0.5f, 0.5f}, {3.0f, 4.0f}, 1234);
        for (int i = 0; i < 60; ++i) sim.step();
        return std::vector<Vec2>(sim.fluid().positions().begin(), sim.fluid().positions().end());
    };
    CHECK(run() == run());
}
