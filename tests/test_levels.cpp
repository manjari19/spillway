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
    REQUIRE(levels.size() == 5);
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
