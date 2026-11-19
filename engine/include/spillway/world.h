#pragma once

// A running level: terrain + fluid + drains/pits + player edits + scoring.
//
// Ending a level: reaching the goal does NOT end it. The water keeps flowing
// until it has settled (nothing reached a drain or pit for kSettleSeconds
// and what is left is nearly still), or the player calls finish(). At that
// moment the result is computed once into an Outcome and the simulation
// freezes, so the score shown can never change afterwards. A loss is also
// only declared once the water has settled.
//
// Determinism: player edits are queued and only applied at the start of the
// next fixed step, in the order they were queued. The same level plus the
// same edits on the same step numbers therefore always produces the same
// particle state, which is what replays and the determinism test rely on.

#include <cstdint>
#include <vector>

#include "spillway/fluid.h"
#include "spillway/level.h"
#include "spillway/simulation.h"
#include "spillway/terrain.h"
#include "spillway/timestep.h"

namespace spillway {

enum class Tool : uint8_t { Dig, Wall, Erase };

// One brush stroke segment. A drag is sent as consecutive segments.
struct Edit {
    Tool tool = Tool::Dig;
    Vec2 from{};
    Vec2 to{};
    float radius = 0.3f;
};

enum class LevelStatus { Playing, Won, Lost };

struct Score {
    int total = 0;    // particles at level start
    int saved = 0;    // reached any drain
    int lost = 0;     // fell in a pit
    int required = 0; // saved needed to win
    // Per drain: particles saved there and the share it needs (levels where
    // several drains each need part of the water).
    std::vector<int> drainSaved;
    std::vector<int> drainRequired;
    float savedFraction() const { return total > 0 ? static_cast<float>(saved) / static_cast<float>(total) : 0.0f; }
};

// The frozen result of a finished level.
struct Outcome {
    LevelStatus status = LevelStatus::Playing;
    Score score;
    int percent = 0; // floor of the saved percentage
    int stars = 0;
    int inkUsed = 0;
};

// Things that happened during the last update, for visual effects. The game
// clears these after reading them.
struct WorldEvents {
    std::vector<Vec2> drained; // where particles reached a drain
    std::vector<Vec2> lost;    // where particles fell into a pit
    std::vector<Vec2> dug;     // centres of dirt cells removed
    void clear() { drained.clear(); lost.clear(); dug.clear(); }
};

// 1 star: won. 2 stars: saved >= level.twoStarPercent. 3 stars: saved >=
// level.threeStarPercent and at most half the ink used. 0 if not won.
int starRating(const LevelDesc& level, const Score& score, int inkUsed, int inkBudget);

// Ink is wall area measured in cells, so erasing refunds exactly what was
// spent. Level files give it in metres of wall at this reference width.
constexpr float kInkReferenceWallWidth = 0.2f;

class World {
public:
    // particleScale: water quality multiplier (1 = normal, used by all tests).
    explicit World(const LevelDesc& level, const SimulationConfig& config = {}, float particleScale = 1.0f);
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void queueEdit(const Edit& edit) { pending_.push_back(edit); }

    // Real frame time in; runs whole fixed steps. Returns steps run.
    int update(double frameSeconds);
    // One fixed step: apply edits, substep the fluid, drains, scoring.
    void step();

    const LevelDesc& level() const { return level_; }
    const Terrain& terrain() const { return terrain_; }
    FluidSystem& fluid() { return fluid_; }
    const FluidSystem& fluid() const { return fluid_; }

    const Score& score() const { return score_; }
    LevelStatus status() const { return outcome_.status; }
    // Enough water saved (in total and in every drain). The level keeps
    // running; the game shows a Finish button.
    bool goalReached() const;
    // No longer possible to reach the goal with the water that is left.
    bool goalImpossible() const;
    // The water has settled after the player acted but the level is not
    // decided: time to offer a restart.
    bool stalled() const { return stalled_; }
    // The level is over and frozen; outcome() holds the final result.
    bool finished() const { return outcome_.status != LevelStatus::Playing; }
    const Outcome& outcome() const { return outcome_; }
    // End now (the Finish button). Only allowed once the goal is reached.
    // Returns true if the level finished.
    bool finish();
    int stars() const { return outcome_.stars; }

    WorldEvents& events() { return events_; }

    int inkBudget() const { return inkBudget_; }
    int inkRemaining() const { return ink_; }
    int inkUsed() const { return inkBudget_ - ink_; }
    float inkMetres(int cells) const;

    uint64_t stepCount() const { return stepCount_; }
    double lastStepMs() const { return lastStepMs_; }
    int lastSdfNodes() const { return lastSdfNodes_; }
    const SimulationConfig& config() const { return config_; }

    static constexpr float kStillMeanSpeed = 0.2f; // m/s
    static constexpr double kSettleSeconds = 2.5;

private:
    void applyEdit(const Edit& edit);
    void buildOccupancy();
    void collectDrainsAndPits();
    void updateStatus();
    void finalize(LevelStatus status);

    template <class Fn>
    static void stampAlong(const Edit& e, Fn&& stamp);

    SimulationConfig config_;
    LevelDesc level_;
    Terrain terrain_;
    FluidSystem fluid_;
    FixedTimestep timestep_;

    std::vector<Edit> pending_;
    std::vector<uint8_t> occupied_; // per cell: overlapped by a particle
    std::vector<uint8_t> removeFlags_;

    Score score_;
    Outcome outcome_;
    int inkBudget_ = 0;
    int ink_ = 0;

    bool everEdited_ = false;
    bool stalled_ = false;
    double secondsSinceActivity_ = 0.0; // since water was last counted or the player edited
    bool settled_ = false;
    WorldEvents events_;

    uint64_t stepCount_ = 0;
    double lastStepMs_ = 0.0;
    int lastSdfNodes_ = 0;
};

} // namespace spillway
