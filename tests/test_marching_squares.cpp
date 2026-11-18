#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "spillway/marching_squares.h"

using namespace spillway;
using Catch::Approx;

namespace {
constexpr int kDirt = 0, kRock = 1, kWall = 2; // indices into kMaterials
}

TEST_CASE("Marching squares: empty terrain has no triangles", "[marching]") {
    Terrain t(40, 30, 0.1f);
    TerrainMesher m;
    m.build(t);
    for (int k = 0; k < 3; ++k) CHECK(m.area(k) == 0.0f);
}

TEST_CASE("Marching squares: fully solid level covers the whole level", "[marching]") {
    Terrain t(40, 30, 0.1f);
    t.fill(Cell::Rock);
    TerrainMesher m;
    m.build(t);
    CHECK(m.area(kRock) == Approx(4.0f * 3.0f).epsilon(1e-4));
    CHECK(m.area(kDirt) == 0.0f);
}

TEST_CASE("Marching squares: a square block keeps its area with rounded corners", "[marching]") {
    const float c = 0.1f;
    Terrain t(40, 40, c);
    for (int y = 10; y < 20; ++y)
        for (int x = 10; x < 20; ++x) t.set(x, y, Cell::Dirt); // 1 x 1 m
    TerrainMesher m;
    m.build(t);
    const float a = m.area(kDirt);
    // Straight edges sit exactly on the cell boundary; only the corners get
    // trimmed, so the area is slightly under 1 m^2.
    CHECK(a < 1.0f);
    CHECK(a > 0.97f);

    // The outline of a straight edge lies on the true cell boundary: the
    // field is 0.75 just inside and 0.25 just outside, so the 0.5 crossing is
    // exactly halfway between the two cell centres.
    CHECK(TerrainMesher::field(t, Cell::Dirt, 10, 15) == Approx(0.75f));
    CHECK(TerrainMesher::field(t, Cell::Dirt, 9, 15) == Approx(0.25f));
}

TEST_CASE("Marching squares: a rasterised circle has close to the right area", "[marching]") {
    const float c = 0.05f, r = 1.0f;
    Terrain t(80, 80, c);
    t.forEachCellInDisc({2.0f, 2.0f}, r, [&](int x, int y) { t.set(x, y, Cell::Wall); });
    TerrainMesher m;
    m.build(t);
    CHECK(m.area(kWall) == Approx(std::numbers::pi_v<float> * r * r).epsilon(0.03));
}

TEST_CASE("Marching squares: saddles join corner contacts and split real gaps", "[marching]") {
    auto twoBlocks = [](int gap) {
        // Two 2x2 blocks on a diagonal; gap = empty cells between them.
        Terrain t(20, 20, 0.1f);
        for (int y = 4; y < 6; ++y)
            for (int x = 4; x < 6; ++x) t.set(x, y, Cell::Dirt);
        for (int y = 6 + gap; y < 8 + gap; ++y)
            for (int x = 6 + gap; x < 8 + gap; ++x) t.set(x, y, Cell::Dirt);
        TerrainMesher m;
        m.build(t);
        return m.area(kDirt);
    };
    Terrain one(20, 20, 0.1f);
    for (int y = 4; y < 6; ++y)
        for (int x = 4; x < 6; ++x) one.set(x, y, Cell::Dirt);
    TerrainMesher m1;
    m1.build(one);
    const float single = m1.area(kDirt);

    // Touching at a corner: collision treats that as closed (the swept test
    // never slips between diagonal cells), so the outline is drawn joined
    // and has a small bridge of extra area.
    CHECK(twoBlocks(0) > 2.0f * single * 1.05f);
    // With a one-cell diagonal gap they are drawn as two separate blobs.
    CHECK(twoBlocks(1) == Approx(2.0f * single).epsilon(0.01));
}

TEST_CASE("Marching squares: incremental re-mesh matches a full rebuild", "[marching]") {
    Terrain t(120, 70, 0.075f);
    t.fill(Cell::Dirt);
    TerrainMesher m;
    m.build(t);
    t.takeMeshDirty();

    t.dig({3.0f, 2.0f}, 0.5f);
    t.dig({6.0f, 4.0f}, 0.3f);
    const int rebuilt = m.update(t);
    CHECK(rebuilt > 0);
    CHECK(rebuilt < m.chunkCount()); // only the edited area

    TerrainMesher full;
    full.build(t);
    for (int ch = 0; ch < m.chunkCount(); ++ch)
        for (int k = 0; k < 3; ++k) REQUIRE(m.triangles(ch, k) == full.triangles(ch, k));
}
