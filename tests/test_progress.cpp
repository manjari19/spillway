#include <catch2/catch_test_macros.hpp>

#include "spillway/json.h"
#include "spillway/progress.h"

using namespace spillway;

TEST_CASE("JSON round-trips and rejects garbage", "[progress][json]") {
    const json::Value v = json::Object{{"a", 1}, {"b", json::Array{true, false, nullptr, "x\"y"}}, {"c", 2.5}};
    const auto back = json::parse(json::dump(v));
    REQUIRE(back);
    CHECK((*back)["a"].number(0) == 1);
    CHECK((*back)["c"].number(0) == 2.5);
    CHECK((*back)["b"].array()->size() == 4);
    for (const char* bad : {"", "{", "{\"a\":}", "[1,2", "{\"a\":1}x", "nul", "{\"a\" 1}", "\"unterminated"})
        CHECK_FALSE(json::parse(bad));
}

TEST_CASE("Progress unlocks levels in order and tracks bests", "[progress]") {
    Progress p;
    p.ensureSize(5);
    CHECK(p.unlocked(0));
    CHECK_FALSE(p.unlocked(1));
    CHECK(p.firstUnbeaten(5) == 0);
    CHECK_FALSE(p.anyProgress());

    CHECK(p.record(0, 1, 72));   // first win is a new best
    CHECK(p.unlocked(1));
    CHECK_FALSE(p.unlocked(2));
    CHECK(p.firstUnbeaten(5) == 1);
    CHECK_FALSE(p.record(0, 1, 70)); // worse: not a new best, nothing lost
    CHECK(p.levels[0].bestPercent == 72);
    CHECK(p.record(0, 1, 80));       // better % with the same stars counts
    CHECK(p.record(0, 3, 75));       // more stars counts
    CHECK(p.levels[0].stars == 3);
    CHECK(p.levels[0].bestPercent == 80);
    CHECK(p.totalStars() == 3);
    CHECK_FALSE(p.record(1, 0, 40)); // a loss records nothing
}

TEST_CASE("Progress survives a save/load round trip; corrupt saves start fresh", "[progress]") {
    Progress p;
    p.record(0, 2, 85);
    p.record(1, 1, 71);
    Progress q;
    REQUIRE(progressFromJson(progressToJson(p), q));
    REQUIRE(q.levels.size() == 2);
    CHECK(q.levels[0].stars == 2);
    CHECK(q.levels[1].bestPercent == 71);

    Progress r;
    CHECK_FALSE(progressFromJson("{\"levels\": [ {\"stars\": 2,", r));
    CHECK(r.levels.empty());
    CHECK_FALSE(progressFromJson("[1,2,3]", r));
    // Out-of-range values are clamped rather than trusted.
    REQUIRE(progressFromJson("{\"levels\":[{\"stars\":9,\"best\":250}]}", r));
    CHECK(r.levels[0].stars == 3);
    CHECK(r.levels[0].bestPercent == 100);
}
