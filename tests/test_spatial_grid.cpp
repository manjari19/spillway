#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "spillway/rng.h"
#include "spillway/spatial_grid.h"

using namespace spillway;

namespace {

std::vector<uint32_t> bruteForceNeighbours(const std::vector<Vec2>& pts, Vec2 p, float h) {
    std::vector<uint32_t> out;
    for (uint32_t j = 0; j < pts.size(); ++j)
        if (lengthSq(pts[j] - p) < h * h) out.push_back(j);
    return out;
}

std::vector<uint32_t> gridNeighbours(const SpatialGrid& grid, const std::vector<Vec2>& pts, Vec2 p, float h) {
    std::vector<uint32_t> out;
    grid.forEachCandidate(p, [&](uint32_t j) {
        if (lengthSq(pts[j] - p) < h * h) out.push_back(j);
    });
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("Spatial grid finds exactly the brute-force neighbours", "[grid]") {
    const Vec2 lo{0, 0}, hi{16, 9};
    for (uint64_t seed : {1ull, 2ull, 3ull, 42ull}) {
        for (float h : {0.25f, 0.35f, 1.0f}) {
            Rng rng(seed);
            std::vector<Vec2> pts(1500);
            for (auto& p : pts) p = {rng.uniform(lo.x, hi.x), rng.uniform(lo.y, hi.y)};

            SpatialGrid grid;
            grid.configure(lo, hi, h);
            grid.build(pts);

            for (const Vec2& p : pts) REQUIRE(gridNeighbours(grid, pts, p, h) == bruteForceNeighbours(pts, p, h));
        }
    }
}

TEST_CASE("Spatial grid handles particles outside the bounds", "[grid]") {
    // Points slightly outside the domain get clamped into edge cells; queries
    // must still return the correct neighbours.
    const Vec2 lo{0, 0}, hi{4, 4};
    const float h = 0.5f;
    Rng rng(7);
    std::vector<Vec2> pts(600);
    for (auto& p : pts) p = {rng.uniform(-1.0f, 5.0f), rng.uniform(-1.0f, 5.0f)};

    SpatialGrid grid;
    grid.configure(lo, hi, h);
    grid.build(pts);
    for (const Vec2& p : pts) REQUIRE(gridNeighbours(grid, pts, p, h) == bruteForceNeighbours(pts, p, h));
}

TEST_CASE("Spatial grid counts every particle exactly once", "[grid]") {
    const Vec2 lo{0, 0}, hi{10, 10};
    Rng rng(11);
    std::vector<Vec2> pts(2000);
    for (auto& p : pts) p = {rng.uniform(0.0f, 10.0f), rng.uniform(0.0f, 10.0f)};
    SpatialGrid grid;
    grid.configure(lo, hi, 0.7f);
    grid.build(pts);

    uint32_t total = 0;
    for (int y = 0; y < grid.cellsY(); ++y)
        for (int x = 0; x < grid.cellsX(); ++x) total += grid.cellCount(x, y);
    CHECK(total == pts.size());
}
