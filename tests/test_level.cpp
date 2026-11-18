#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "spillway/level.h"

using namespace spillway;
using Catch::Matchers::ContainsSubstring;

namespace {
const char* kValid = R"(
# comment line
name  Test Level
hint  Do the thing.   # trailing comment
size  8 6
cell  0.1
win   60
ink   3.5
dirt  rect 0 0 8 6
empty circle 4 3 1.5
rock  poly 0 6  8 6  8 5.5
water rect 3 2 5 3
drain rect 6 5 8 6
pit   rect 0 5 1 6
)";
}

TEST_CASE("A valid level file parses", "[level]") {
    const auto r = parseLevel(kValid);
    REQUIRE(r);
    const LevelDesc& l = *r.level;
    CHECK(l.name == "Test Level");
    CHECK(l.hint == "Do the thing.");
    CHECK(l.size == Vec2{8, 6});
    CHECK(l.cellSize == 0.1f);
    CHECK(l.winPercent == 60.0f);
    CHECK(l.inkMetres == 3.5f);
    CHECK(l.terrain.size() == 3);
    CHECK(l.water.size() == 1);
    CHECK(l.drains.size() == 1);
    CHECK(l.pits.size() == 1);

    const Terrain t = buildTerrain(l);
    CHECK(t.width() == 80);
    CHECK(t.height() == 60);
    CHECK(t.atPoint({4, 3}) == Cell::Empty);   // inside the circle
    CHECK(t.atPoint({0.5f, 0.5f}) == Cell::Dirt);
    CHECK(t.atPoint({7.9f, 5.9f}) == Cell::Rock); // inside the polygon
}

TEST_CASE("Malformed level files are rejected with a clear error", "[level]") {
    struct Case { const char* text; const char* expect; };
    const Case cases[] = {
        {"size 8 6\nwin 50\ndrian rect 0 0 1 1\n", "line 3: unknown command 'drian'"},
        {"size 8 6\nwin fifty\n", "line 2: expected a number in win"},
        {"size 8\n", "line 1: size needs 2 numbers"},
        {"size 8 6\nwin 50\ndirt poly 0 0 1 1\n", "poly needs at least 3"},
        {"size 8 6\nwin 50\ndirt hexagon 1 2 3\n", "unknown shape 'hexagon'"},
        {"size 8 6\nwin 50\nwater rect 2 2 1 1\n", "x1 > x0"},
        {"size 8 6\nwin 50\nwall rect 0 0 1 1\n", "walls are drawn by the player"},
        {"size 8 6\nwin 150\n", "win must be a percentage"},
        {"size 8 6\nwin 50\nwater rect 0 0 9 1\n", "outside the level"},
        {"win 50\nwater rect 0 0 1 1\ndrain rect 1 1 2 2\n", "missing required 'size'"},
        {"size 8 6\nwin 50\nwater rect 0 0 1 1\n", "no 'drain'"},
        {"size 8 6\nwin 50\ndrain rect 0 0 1 1\n", "no 'water'"},
    };
    for (const Case& c : cases) {
        INFO(c.text);
        const auto r = parseLevel(c.text);
        CHECK_FALSE(r);
        CHECK_THAT(r.error, ContainsSubstring(c.expect));
    }
}

TEST_CASE("Missing level file reports the path", "[level]") {
    const auto r = loadLevelFile("does/not/exist.txt");
    CHECK_FALSE(r);
    CHECK_THAT(r.error, ContainsSubstring("does/not/exist.txt"));
}

TEST_CASE("Shipped level files load", "[level]") {
    const auto r = loadLevelFile(std::string(SPILLWAY_LEVEL_DIR) + "/level01.txt");
    INFO(r.error);
    REQUIRE(r);
    CHECK(r.level->winPercent == 70.0f);
}
