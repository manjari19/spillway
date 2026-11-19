#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "solution_replay.h"
#include "spillway/fluid.h"
#include "spillway/level.h"
#include "spillway/thread_pool.h"
#include "spillway/world.h"

using namespace spillway;

namespace {

struct Snapshot {
    std::vector<Vec2> pos, vel;
    std::vector<float> density;
};

bool bitIdentical(const Snapshot& a, const Snapshot& b) {
    return a.pos.size() == b.pos.size() && a.vel.size() == b.vel.size() &&
           std::memcmp(a.pos.data(), b.pos.data(), a.pos.size() * sizeof(Vec2)) == 0 &&
           std::memcmp(a.vel.data(), b.vel.data(), a.vel.size() * sizeof(Vec2)) == 0 &&
           std::memcmp(a.density.data(), b.density.data(), a.density.size() * sizeof(float)) == 0;
}

Snapshot snap(const FluidSystem& f) {
    return {{f.positions().begin(), f.positions().end()},
            {f.velocities().begin(), f.velocities().end()},
            {f.densities().begin(), f.densities().end()}};
}

// A dam break in a box: a block of water collapsing and sloshing.
Snapshot runBox(int threads, int steps) {
    ThreadPool pool(threads);
    FluidSystem f(FluidParams{}, {0, 0}, {12, 8});
    f.spawnBlock({0.2f, 0.8f}, {6.6f, 7.8f}, 7);
    f.setThreadPool(&pool);
    for (int i = 0; i < steps; ++i) f.step(1.0f / 240.0f);
    return snap(f);
}

std::vector<int> threadCounts() {
    std::vector<int> t{1, 2, 4};
    const int n = ThreadPool::hardwareThreads();
    if (n != 1 && n != 2 && n != 4) t.push_back(n);
    return t;
}

} // namespace

TEST_CASE("parallelFor visits every index exactly once", "[threads]") {
    for (int threads : {1, 3, 8}) {
        ThreadPool pool(threads);
        for (size_t n : {size_t{0}, size_t{1}, size_t{255}, size_t{1000}, size_t{12345}}) {
            std::vector<std::atomic<int>> hits(n);
            for (int rep = 0; rep < 3; ++rep)
                pool.parallelFor(n, [&](size_t b, size_t e) {
                    for (size_t i = b; i < e; ++i) hits[i].fetch_add(1);
                });
            bool ok = true;
            for (auto& h : hits) ok = ok && h.load() == 3;
            CHECK(ok);
        }
    }
}

TEST_CASE("The fluid is bit-identical for any thread count", "[threads][determinism]") {
    // 600 substeps of a ~2,400 particle dam break.
    const Snapshot ref = runBox(1, 600);
    REQUIRE(ref.pos.size() > 2000);
    for (int t : threadCounts()) {
        INFO(t << " threads");
        CHECK(bitIdentical(runBox(t, 600), ref));
    }
}

TEST_CASE("A level with terrain, edits, drains and pits is bit-identical for any thread count", "[threads][determinism]") {
    // Level 10 played by its recorded solution: digging, a wall, two sources,
    // pits and drains. Compared after 600 fixed steps (2,400 substeps).
    const auto level = loadLevelFile(std::string(SPILLWAY_LEVEL_DIR) + "/level10.txt");
    REQUIRE(level);
    const replay::Solution sol = replay::loadSolution(std::string(SPILLWAY_SOLUTION_DIR) + "/level10.txt");
    REQUIRE(sol.error.empty());
    auto run = [&](int threads) {
        ThreadPool pool(threads);
        World w(*level.level);
        w.fluid().setThreadPool(&pool);
        size_t next = 0;
        for (int step = 0; step < 600; ++step) {
            while (next < sol.edits.size() && sol.edits[next].step <= step) w.queueEdit(sol.edits[next++].edit);
            w.step();
            w.events().clear();
        }
        Snapshot s = snap(w.fluid());
        s.density.push_back(static_cast<float>(w.score().saved));
        s.density.push_back(static_cast<float>(w.score().lost));
        return s;
    };
    const Snapshot ref = run(1);
    for (int t : threadCounts()) {
        INFO(t << " threads");
        CHECK(bitIdentical(run(t), ref));
    }
}
