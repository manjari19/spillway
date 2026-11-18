#pragma once

// A running level: terrain + fluid + drains/pits + player edits + scoring.
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
    int saved = 0;    // reached a drain
    int lost = 0;     // fell in a pit
    int required = 0; // saved needed to win
    float savedFraction() const { return total > 0 ? static_cast<float>(saved) / static_cast<float>(total) : 0.0f; }
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
    explicit World(const LevelDesc& level, const SimulationConfig& config = {});
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
    LevelStatus status() const { return status_; }
    // True when the player has acted, the water has gone still and nothing
    // has reached the drain for a few seconds: time to offer a restart.
    bool stalled() const { return stalled_; }
    // Lost, or won and the flow has died down (no water reached a drain for
    // kSettleSeconds), so the final percentage is stable enough to score.
    bool finished() const;
    int stars() const { return starRating(level_, score_, inkUsed(), inkBudget_); }

    WorldEvents& events() { return events_; }

    int inkBudget() const { return inkBudget_; }
    int inkRemaining() const { return ink_; }
    int inkUsed() const { return inkBudget_ - ink_; }
    float inkMetres(int cells) const;

    uint64_t stepCount() const { return stepCount_; }
    double lastStepMs() const { return lastStepMs_; }
    int lastSdfNodes() const { return lastSdfNodes_; }
    const SimulationConfig& config() const { return config_; }

    static constexpr double kStallSeconds = 3.0;
    static constexpr float kStillMeanSpeed = 0.2f; // m/s
    static constexpr double kSettleSeconds = 2.5;

private:
    void applyEdit(const Edit& edit);
    void buildOccupancy();
    void collectDrainsAndPits();
    void updateStatus();

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
    LevelStatus status_ = LevelStatus::Playing;
    int inkBudget_ = 0;
    int ink_ = 0;

    bool everEdited_ = false;
    bool stalled_ = false;
    double quietSeconds_ = 0.0;
    double secondsSinceSave_ = 0.0;
    WorldEvents events_;

    uint64_t stepCount_ = 0;
    double lastStepMs_ = 0.0;
    int lastSdfNodes_ = 0;
};

} // namespace spillway
