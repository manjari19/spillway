#include <catch2/catch_test_macros.hpp>

#include "spillway/fluid.h"
#include "spillway/terrain.h"

using namespace spillway;

namespace {

constexpr float kCell = 0.075f;

FluidParams noGravity() {
    FluidParams p;
    p.gravity = {0, 0};
    return p;
}

void runSubsteps(FluidSystem& f, int n) {
    for (int i = 0; i < n; ++i) f.step(1.0f / 240.0f);
}

} // namespace

TEST_CASE("Fast particles never tunnel through rock or thin walls", "[collision][tunneling]") {
    for (Cell material : {Cell::Rock, Cell::Wall}) {
        for (int thickness : {2, 3, 8}) {
            // 8 x 4 m box, vertical slab starting at x = 4.0 m.
            Terrain t(107, 54, kCell);
            const int slabX = 53;
            for (int y = 0; y < t.height(); ++y)
                for (int x = slabX; x < slabX + thickness; ++x) t.set(x, y, material);
            t.updateSdf();
            const float slabLeft = slabX * kCell;

            FluidSystem f(noGravity(), {0, 0}, t.extent());
            f.setTerrain(&t);
            // Particles fired right at 200 m/s (far beyond the speed limit).
            for (int k = 0; k < 300; ++k) {
                const float y = 0.2f + static_cast<float>(k % 30) * 0.12f;
                const float x = 0.5f + static_cast<float>(k / 30) * 0.3f;
                f.addParticle({x, y}, {200.0f, (k % 7 - 3) * 20.0f});
            }
            for (int s = 0; s < 400; ++s) {
                f.step(1.0f / 240.0f);
                for (const Vec2 p : f.positions()) {
                    REQUIRE(p.x < slabLeft);
                    REQUIRE_FALSE(isSolid(t.atPoint(p)));
                }
            }
        }
    }
}

TEST_CASE("Fast particles never tunnel through a diagonal stroke", "[collision][tunneling]") {
    // A diagonal wall made of discs, like a player stroke with the smallest
    // brush. Diagonal edges are where corner gaps would show up.
    // Corner to corner across an 8 x 8 m box, so nothing can go around it.
    Terrain t(107, 107, kCell);
    for (float s = 0.0f; s <= 1.0f; s += 0.005f) {
        const Vec2 c{8.0f * s, 8.0f - 8.0f * s};
        t.forEachCellInDisc(c, 0.08f, [&](int x, int y) { t.set(x, y, Cell::Wall); });
    }
    t.updateSdf();

    FluidSystem f(noGravity(), {0, 0}, t.extent());
    f.setTerrain(&t);
    // All particles start where x + y <= 6.5 and are fired straight at the
    // line x + y = 8 at ~210 m/s, with some spread.
    for (int k = 0; k < 200; ++k) {
        const float a = 0.5f + static_cast<float>(k % 20) * 0.15f;
        const float b = 0.5f + static_cast<float>(k / 20) * 0.25f;
        f.addParticle({a, b}, {150.0f + (k % 5) * 10.0f, 150.0f - (k % 3) * 10.0f});
    }
    for (int s = 0; s < 400; ++s) {
        f.step(1.0f / 240.0f);
        for (const Vec2 p : f.positions()) {
            REQUIRE(p.x + p.y < 8.0f); // stays on the starting side
            REQUIRE_FALSE(isSolid(t.atPoint(p)));
        }
    }
}

TEST_CASE("Digging a floor lets water through; drawing a wall stops it", "[collision][editing]") {
    Terrain t(60, 80, kCell); // 4.5 x 6 m
    // Dirt floor at y = 3 m.
    for (int y = 40; y < 44; ++y)
        for (int x = 0; x < 60; ++x) t.set(x, y, Cell::Dirt);
    t.updateSdf();
    const float floorTop = 40 * kCell;

    FluidSystem f(FluidParams{}, {0, 0}, t.extent());
    f.setTerrain(&t);
    f.spawnBlock({1.0f, 1.0f}, {3.0f, 2.0f});
    runSubsteps(f, 480);
    for (const Vec2 p : f.positions()) REQUIRE(p.y < floorTop);

    // Dig the whole floor out: water falls through.
    for (float x = 0.0f; x < 4.5f; x += 0.1f) t.dig({x, 3.15f}, 0.4f);
    t.updateSdf();
    runSubsteps(f, 480);
    int below = 0;
    for (const Vec2 p : f.positions()) below += p.y > floorTop + 0.3f;
    CHECK(below > static_cast<int>(f.size()) * 9 / 10);

    // Fresh water falling onto a newly drawn wall at y = 1.5 m.
    FluidSystem g(FluidParams{}, {0, 0}, t.extent());
    g.setTerrain(&t);
    g.spawnBlock({1.0f, 0.2f}, {3.0f, 1.0f});
    for (int x = 0; x < 60; ++x)
        for (int y = 20; y < 23; ++y) t.set(x, y, Cell::Wall);
    t.updateSdf();
    runSubsteps(g, 480);
    for (const Vec2 p : g.positions()) REQUIRE(p.y < 20 * kCell);
}
