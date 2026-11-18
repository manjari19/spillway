#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "spillway/terrain.h"

using namespace spillway;
using Catch::Approx;

TEST_CASE("SDF around a single solid cell", "[terrain][sdf]") {
    const float c = 0.1f;
    Terrain t(20, 20, c);
    t.set(10, 10, Cell::Rock); // occupies [1.0, 1.1] x [1.0, 1.1]
    t.updateSdf();

    // The cell's own corners are on the surface.
    CHECK(t.nodeDistance(10, 10) == Approx(0.0f).margin(1e-6));
    CHECK(t.nodeDistance(11, 11) == Approx(0.0f).margin(1e-6));
    // One node to the right of the cell's right edge: one cell away.
    CHECK(t.nodeDistance(12, 10) == Approx(c));
    // Diagonal from the bottom-right corner.
    CHECK(t.nodeDistance(12, 12) == Approx(c * std::sqrt(2.0f)));
    CHECK(t.nodeDistance(13, 10) == Approx(2 * c));

    // Interpolated: halfway between the right face and the next node line.
    CHECK(t.distance({1.15f, 1.05f}) == Approx(0.5f * c));
    // Above the top face, 0.25 cells up.
    CHECK(t.distance({1.05f, 0.975f}) == Approx(0.25f * c));
    // Gradient points away from the solid.
    const Vec2 g = t.gradient({1.15f, 1.05f});
    CHECK(g.x == Approx(1.0f));
    CHECK(g.y == Approx(0.0f).margin(1e-5));

    // Inside the cell the field is <= 0.
    CHECK(t.distance({1.05f, 1.05f}) <= 0.0f);
}

TEST_CASE("SDF of a horizontal line and inside a thick block", "[terrain][sdf]") {
    const float c = 0.1f;
    Terrain t(30, 30, c);
    for (int x = 0; x < 30; ++x) t.set(x, 15, Cell::Wall); // y in [1.5, 1.6]
    for (int y = 20; y < 30; ++y)
        for (int x = 5; x < 25; ++x) t.set(x, y, Cell::Rock); // block, top face at y = 2.0
    t.updateSdf();

    // Above the line: distance is the vertical gap, anywhere along x.
    for (float x : {0.55f, 1.23f, 2.71f}) {
        CHECK(t.distance({x, 1.45f}) == Approx(0.05f).margin(1e-5));
        CHECK(t.distance({x, 1.3f}) == Approx(0.2f).margin(1e-5));
    }
    // Inside the block: negative distance to the nearest empty cell.
    CHECK(t.nodeDistance(15, 22) == Approx(-2 * c)); // 2 cells below the top face
    CHECK(t.nodeDistance(15, 23) == Approx(-3 * c));
    // Far from any solid the value is clamped to the band.
    Terrain open(30, 30, c);
    open.set(0, 0, Cell::Rock);
    open.updateSdf();
    CHECK(open.nodeDistance(20, 20) == Approx(Terrain::kSdfBand * c));
}

TEST_CASE("SDF updates only the dirty region and matches a full rebuild", "[terrain][sdf]") {
    Terrain a(60, 40, 0.075f);
    for (int y = 25; y < 40; ++y)
        for (int x = 0; x < 60; ++x) a.set(x, y, Cell::Dirt);
    a.rebuildSdf();

    // Dig one disc. The recomputed region should be the edit's bounding box
    // grown by the band, not the whole level.
    const int changed = a.dig({2.0f, 1.9f}, 0.3f);
    REQUIRE(changed > 0);
    const int nodes = a.updateSdf();
    CHECK(nodes > 0);
    CHECK(nodes < 61 * 41 / 4);
    const CellRect r = a.lastSdfRegion();
    CHECK(r.x1 - r.x0 <= 2 * static_cast<int>(std::ceil(0.3f / 0.075f)) + 2 * Terrain::kSdfBand + 1);

    Terrain b = a;
    b.rebuildSdf();
    for (int j = 0; j <= 40; ++j)
        for (int i = 0; i <= 60; ++i) REQUIRE(a.nodeDistance(i, j) == b.nodeDistance(i, j));
}

TEST_CASE("Digging removes dirt only; erasing removes walls only", "[terrain]") {
    Terrain t(20, 20, 0.1f);
    t.set(5, 5, Cell::Dirt);
    t.set(6, 5, Cell::Rock);
    t.set(7, 5, Cell::Wall);
    CHECK(t.dig({0.65f, 0.55f}, 0.25f) == 1);
    CHECK(t.at(5, 5) == Cell::Empty);
    CHECK(t.at(6, 5) == Cell::Rock);
    CHECK(t.at(7, 5) == Cell::Wall);
    CHECK(t.erase({0.65f, 0.55f}, 0.25f) == 1);
    CHECK(t.at(7, 5) == Cell::Empty);
    CHECK(t.at(6, 5) == Cell::Rock);
}

TEST_CASE("Swept segment test finds solid cells, including corner crossings", "[terrain]") {
    Terrain t(10, 10, 0.1f);
    t.set(5, 5, Cell::Rock);
    float tHit = -1.0f;
    CHECK(t.segmentHitsSolid({0.45f, 0.55f}, {0.65f, 0.55f}, tHit));
    CHECK(tHit == Approx(0.25f));
    CHECK_FALSE(t.segmentHitsSolid({0.1f, 0.1f}, {0.3f, 0.3f}, tHit));

    // Two cells touching only at a corner: a diagonal through that exact
    // corner must still hit one of them.
    Terrain d(10, 10, 0.1f);
    d.set(4, 4, Cell::Wall);
    d.set(5, 5, Cell::Wall);
    CHECK(d.segmentHitsSolid({0.55f, 0.45f}, {0.45f, 0.55f}, tHit));
}
