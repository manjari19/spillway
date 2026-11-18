#include <catch2/catch_test_macros.hpp>

#include "spillway/timestep.h"

using namespace spillway;

TEST_CASE("Fixed timestep runs one step per 1/60 s regardless of frame rate", "[timestep]") {
    // One simulated second split into frames of different lengths must always
    // produce 60 physics steps.
    for (int fps : {30, 60, 120, 144, 240}) {
        FixedTimestep ts(1.0 / 60.0, 1000);
        int steps = 0;
        for (int f = 0; f < fps; ++f) steps += ts.advance(1.0 / fps);
        CHECK(steps == 60);
    }
}

TEST_CASE("Fixed timestep caps steps per frame and drops the backlog", "[timestep]") {
    FixedTimestep ts(1.0 / 60.0, 4);
    CHECK(ts.advance(0.2) == 4); // would be 12 steps; capped
    CHECK(ts.advance(0.0) == 0); // backlog was dropped, not carried over
}

TEST_CASE("Fixed timestep accumulates small frames", "[timestep]") {
    FixedTimestep ts(1.0 / 60.0, 4);
    CHECK(ts.advance(0.01) == 0);
    CHECK(ts.advance(0.01) == 1);
    CHECK(ts.alpha() > 0.0);
    CHECK(ts.alpha() < 1.0);
}
