#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "solution_replay.h"
#include "spillway/level.h"
#include "spillway/world.h"

using namespace spillway;
namespace fs = std::filesystem;

namespace {

std::vector<fs::path> shippedLevels() {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(SPILLWAY_LEVEL_DIR)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("level", 0) == 0 && e.path().extension() == ".txt") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("Every level file in game/levels loads", "[levels]") {
    const auto levels = shippedLevels();
    REQUIRE(levels.size() == 10);
    for (const auto& p : levels) {
        INFO(p.string());
        const auto r = loadLevelFile(p.string());
        INFO(r.error);
        CHECK(r);
    }
    // The menu backdrop is not a playable level but must load too.
    const auto menu = loadLevelFile(std::string(SPILLWAY_LEVEL_DIR) + "/menu.txt", /*requireGoals=*/false);
    INFO(menu.error);
    CHECK(menu);
}

TEST_CASE("Every shipped level is beaten by its recorded solution", "[levels][solutions]") {
    for (const auto& p : shippedLevels()) {
        const std::string name = p.stem().string(); // levelNN
        const std::string solutionPath = std::string(SPILLWAY_SOLUTION_DIR) + "/" + name + ".txt";
        INFO(name);
        const auto level = loadLevelFile(p.string());
        REQUIRE(level);
        const replay::Solution s = replay::loadSolution(solutionPath);
        INFO(s.error);
        REQUIRE(s.error.empty());

        World w(*level.level);
        replay::run(w, s);
        REQUIRE(w.finished());
        const Outcome& o = w.outcome();
        INFO("percent " << o.percent << ", stars " << o.stars);
        CHECK((o.status == LevelStatus::Won) == s.expectWin);
        CHECK(o.stars == s.expectStars);
    }
}

TEST_CASE("No shipped level leaks water when left untouched", "[levels]") {
    // With no player edits, every pool must hold its water: nothing may reach
    // a drain or a pit, the pools must come to rest, and no particle may end
    // up far from where it started. Pools on tilted floors slosh into their
    // low corner (up to ~1.3 m in the shipped levels); a gap in a rim or a
    // floor would carry water several metres away. Nothing is removed, so a
    // particle keeps its index and can be compared with its start position.
    constexpr float kMaxDisplacement = 2.0f; // metres
    for (const auto& p : shippedLevels()) {
        INFO(p.filename().string());
        const auto level = loadLevelFile(p.string());
        REQUIRE(level);
        World w(*level.level);
        const std::vector<Vec2> start(w.fluid().positions().begin(), w.fluid().positions().end());
        for (int s = 0; s < 60 * 20; ++s) w.step();
        CHECK(w.score().saved == 0);
        CHECK(w.score().lost == 0);
        REQUIRE(w.fluid().size() == start.size());
        float maxMove = 0.0f, meanSpeed = 0.0f;
        for (size_t i = 0; i < start.size(); ++i) {
            maxMove = std::max(maxMove, length(w.fluid().positions()[i] - start[i]));
            meanSpeed += length(w.fluid().velocities()[i]);
        }
        meanSpeed /= static_cast<float>(start.size());
        INFO("max displacement " << maxMove << " m, mean speed " << meanSpeed << " m/s");
        CHECK(maxMove < kMaxDisplacement);
        CHECK(meanSpeed < World::kStillMeanSpeed);
    }
}
