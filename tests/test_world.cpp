#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "spillway/world.h"

using namespace spillway;

namespace {

LevelDesc parseOrFail(const char* text) {
    auto r = parseLevel(text);
    INFO(r.error);
    REQUIRE(r);
    return *r.level;
}

// Water directly above a floor that is part drain, part pit.
LevelDesc splitLevel(float drainWidth) {
    LevelDesc l;
    l.size = {6, 4};
    l.winPercent = 50;
    l.inkMetres = 4;
    l.water.push_back({{0.5f, 0.5f}, {5.5f, 1.5f}});
    l.drains.push_back({{0, 3.5f}, {drainWidth, 4}});
    if (drainWidth < 6) l.pits.push_back({{drainWidth, 3.5f}, {6, 4}});
    return l;
}

int runUntilDone(World& w, int maxSteps) {
    int s = 0;
    for (; s < maxSteps && w.status() == LevelStatus::Playing; ++s) w.step();
    return s;
}

} // namespace

TEST_CASE("All water into a drain wins", "[world][score]") {
    World w(splitLevel(6.0f));
    runUntilDone(w, 600);
    CHECK(w.status() == LevelStatus::Won);
    CHECK(w.score().saved >= w.score().required);
}

TEST_CASE("All water into a pit loses as soon as winning is impossible", "[world][score]") {
    World w(splitLevel(0.01f)); // a sliver of drain, the rest is pit
    runUntilDone(w, 600);
    CHECK(w.status() == LevelStatus::Lost);
    // Lost exactly when saved + alive dropped below required.
    CHECK(w.score().saved + static_cast<int>(w.fluid().size()) < w.score().required);
}

TEST_CASE("Drain and pit counting is exact and never double counts", "[world][score]") {
    LevelDesc l = splitLevel(3.0f);
    // Overlap a pit with the drain: particles there must count as saved once.
    l.pits.push_back({{2.0f, 3.5f}, {4.0f, 4.0f}});
    World w(l);
    const int total = w.score().total;
    REQUIRE(total > 0);
    for (int s = 0; s < 600; ++s) {
        w.step();
        const Score& sc = w.score();
        REQUIRE(sc.saved + sc.lost + static_cast<int>(w.fluid().size()) == total);
    }
    CHECK(w.fluid().size() == 0);
    CHECK(w.score().saved > 0);
    CHECK(w.score().lost > 0);
}

TEST_CASE("Status latches once the level is decided", "[world][score]") {
    World w(splitLevel(6.0f));
    runUntilDone(w, 600);
    REQUIRE(w.status() == LevelStatus::Won);
    for (int s = 0; s < 60; ++s) w.step();
    CHECK(w.status() == LevelStatus::Won);
}

TEST_CASE("Wall ink: spent per cell, refunded exactly by erase", "[world][editing]") {
    LevelDesc l = splitLevel(6.0f);
    l.water = {{{0.2f, 0.2f}, {1.0f, 0.6f}}}; // small puddle out of the way
    World w(l);
    const int budget = w.inkRemaining();
    REQUIRE(budget > 0);

    w.queueEdit({Tool::Wall, {2.0f, 2.0f}, {4.0f, 2.2f}, 0.12f});
    w.step();
    const int walls = w.terrain().count(Cell::Wall);
    CHECK(walls > 0);
    CHECK(w.inkRemaining() == budget - walls);

    w.queueEdit({Tool::Erase, {1.8f, 2.1f}, {4.2f, 2.1f}, 0.5f});
    w.step();
    CHECK(w.terrain().count(Cell::Wall) == 0);
    CHECK(w.inkRemaining() == budget);
}

TEST_CASE("Walls stop when ink runs out and never cover particles", "[world][editing]") {
    LevelDesc l = splitLevel(6.0f);
    l.inkMetres = 0.5f;
    World w(l);
    // A long stroke straight through the water.
    w.queueEdit({Tool::Wall, {0.2f, 1.0f}, {5.8f, 1.0f}, 0.15f});
    w.step();
    CHECK(w.inkRemaining() >= 0);
    CHECK(w.terrain().count(Cell::Wall) == w.inkBudget() - w.inkRemaining());
    for (const Vec2 p : w.fluid().positions()) REQUIRE_FALSE(isSolid(w.terrain().atPoint(p)));
}

TEST_CASE("Stall prompt appears once water settles after the player acts", "[world]") {
    // A sealed basin: the water can never reach the drain.
    const LevelDesc l = parseOrFail(R"(
size 6 4
win 50
dirt rect 0 2 6 4
water rect 0.5 0.5 5.5 1.9
drain rect 5 3.5 6 4
)");
    World w(l);
    for (int s = 0; s < 900; ++s) w.step();
    CHECK_FALSE(w.stalled()); // no edits yet: never prompt

    w.queueEdit({Tool::Dig, {0.5f, 3.5f}, {0.5f, 3.5f}, 0.2f}); // irrelevant dig
    bool stalled = false;
    for (int s = 0; s < 900 && !stalled; ++s) { w.step(); stalled = w.stalled(); }
    CHECK(stalled);
    CHECK(w.status() == LevelStatus::Playing);
}

TEST_CASE("Determinism holds with a scripted sequence of edits", "[world][determinism]") {
    const auto level = loadLevelFile(std::string(SPILLWAY_LEVEL_DIR) + "/level01.txt");
    REQUIRE(level);

    auto run = [&] {
        World w(*level.level);
        for (int s = 0; s < 360; ++s) {
            // Dig through the rim over several frames, then draw a wall.
            if (s >= 10 && s < 40) {
                const float y = 2.0f + 0.05f * static_cast<float>(s - 10);
                w.queueEdit({Tool::Dig, {5.0f, y}, {6.2f, y + 0.1f}, 0.35f});
            }
            if (s == 60) w.queueEdit({Tool::Wall, {8.0f, 3.0f}, {9.5f, 3.2f}, 0.1f});
            if (s == 120) w.queueEdit({Tool::Erase, {8.5f, 3.1f}, {8.5f, 3.1f}, 0.2f});
            w.step();
        }
        return std::make_pair(std::vector<Vec2>(w.fluid().positions().begin(), w.fluid().positions().end()),
                              w.score().saved);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.first.size() == b.first.size());
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}

TEST_CASE("Star rating uses water saved and ink used", "[world][score]") {
    LevelDesc l;
    l.winPercent = 70;
    l.twoStarPercent = 82;
    l.threeStarPercent = 92;
    Score s;
    s.total = 100;
    s.required = 70;

    s.saved = 69;
    CHECK(starRating(l, s, 0, 100) == 0);
    s.saved = 70;
    CHECK(starRating(l, s, 0, 100) == 1);
    s.saved = 85;
    CHECK(starRating(l, s, 0, 100) == 2);
    s.saved = 95;
    CHECK(starRating(l, s, 50, 100) == 3);  // exactly half the ink: still thrifty
    CHECK(starRating(l, s, 51, 100) == 2);  // too much ink
    CHECK(starRating(l, s, 0, 0) == 3);     // level without ink
}

TEST_CASE("Default star thresholds come from the win percentage", "[level]") {
    const auto r = parseLevel("size 4 4\nwin 60\nwater rect 0 0 1 1\ndrain rect 3 3 4 4\n");
    REQUIRE(r);
    CHECK(r.level->twoStarPercent == Catch::Approx(76.0f));
    CHECK(r.level->threeStarPercent == Catch::Approx(88.0f));
}

TEST_CASE("Won levels finish once the flow settles", "[world]") {
    World w(splitLevel(6.0f));
    int s = 0;
    while (!w.finished() && s < 1200) { w.step(); ++s; }
    CHECK(w.finished());
    CHECK(w.status() == LevelStatus::Won);
    CHECK(w.stars() >= 1);
    CHECK_FALSE(w.events().drained.empty());
}
