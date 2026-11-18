#include "spillway/world.h"

#include <chrono>
#include <cmath>

namespace spillway {

World::World(const LevelDesc& level, const SimulationConfig& config)
    : config_(config),
      level_(level),
      terrain_(buildTerrain(level)),
      fluid_(FluidParams{}, {0.0f, 0.0f}, terrain_.extent()),
      timestep_(config.stepSeconds, config.maxStepsPerFrame) {
    fluid_.setTerrain(&terrain_);

    // Spawn water, then drop any particle that landed inside or touching
    // solid terrain (e.g. a water rect overlapping a dirt edge).
    for (size_t i = 0; i < level_.water.size(); ++i) {
        fluid_.spawnBlock(level_.water[i].min, level_.water[i].max, 1000 + i);
    }
    removeFlags_.assign(fluid_.size(), 0);
    const float r = fluid_.params().particleRadius;
    for (size_t i = 0; i < fluid_.size(); ++i) {
        removeFlags_[i] = terrain_.distance(fluid_.positions()[i]) < r ? 1 : 0;
    }
    fluid_.removeMarked(removeFlags_);

    score_.total = static_cast<int>(fluid_.size());
    score_.required = static_cast<int>(std::ceil(score_.total * level_.winPercent / 100.0 - 1e-9));

    const float c = terrain_.cellSize();
    inkBudget_ = static_cast<int>(std::lround(level_.inkMetres * kInkReferenceWallWidth / (c * c)));
    ink_ = inkBudget_;
}

int starRating(const LevelDesc& level, const Score& score, int inkUsed, int inkBudget) {
    if (score.total <= 0 || score.saved < score.required) return 0;
    const float pct = 100.0f * score.savedFraction();
    int stars = 1;
    if (pct >= level.twoStarPercent) stars = 2;
    const bool thrifty = inkBudget <= 0 || inkUsed * 2 <= inkBudget;
    if (stars == 2 && pct >= level.threeStarPercent && thrifty) stars = 3;
    return stars;
}

bool World::finished() const {
    if (status_ == LevelStatus::Lost) return true;
    if (status_ != LevelStatus::Won) return false;
    return fluid_.size() == 0 || secondsSinceSave_ >= kSettleSeconds;
}

float World::inkMetres(int cells) const {
    const float c = terrain_.cellSize();
    return static_cast<float>(cells) * c * c / kInkReferenceWallWidth;
}

int World::update(double frameSeconds) {
    const int steps = timestep_.advance(frameSeconds);
    for (int s = 0; s < steps; ++s) step();
    return steps;
}

void World::step() {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Player edits, in input order.
    if (!pending_.empty()) {
        bool wallEdits = false;
        for (const Edit& e : pending_) wallEdits |= e.tool == Tool::Wall;
        if (wallEdits) buildOccupancy();
        for (const Edit& e : pending_) applyEdit(e);
        pending_.clear();
        everEdited_ = true;
        quietSeconds_ = 0.0;
    }
    lastSdfNodes_ = terrain_.updateSdf();

    // 2. Fluid substeps.
    const float dt = static_cast<float>(config_.stepSeconds / config_.substeps);
    for (int s = 0; s < config_.substeps; ++s) fluid_.step(dt);

    // 3. Drains and pits, then win/lose.
    collectDrainsAndPits();
    updateStatus();

    ++stepCount_;
    const auto t1 = std::chrono::steady_clock::now();
    lastStepMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

template <class Fn>
void World::stampAlong(const Edit& e, Fn&& stamp) {
    // Stamp discs along the segment, spaced at half the radius so a fast
    // drag still leaves a continuous stroke.
    const float len = length(e.to - e.from);
    const float spacing = std::max(0.02f, e.radius * 0.5f);
    const int n = static_cast<int>(std::ceil(len / spacing));
    for (int k = 0; k <= n; ++k) {
        const float t = n > 0 ? static_cast<float>(k) / static_cast<float>(n) : 0.0f;
        stamp(e.from + (e.to - e.from) * t);
    }
}

void World::buildOccupancy() {
    // Mark every cell overlapped by a particle's collision disc. Walls are
    // never placed on these cells, so a stroke cannot trap water inside solid.
    const int w = terrain_.width(), h = terrain_.height();
    occupied_.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    const float r = fluid_.params().particleRadius;
    for (const Vec2 p : fluid_.positions()) {
        const int x0 = std::max(0, terrain_.cellX(p.x - r)), x1 = std::min(w - 1, terrain_.cellX(p.x + r));
        const int y0 = std::max(0, terrain_.cellY(p.y - r)), y1 = std::min(h - 1, terrain_.cellY(p.y + r));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) occupied_[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 1;
    }
}

void World::applyEdit(const Edit& e) {
    switch (e.tool) {
    case Tool::Dig:
        stampAlong(e, [&](Vec2 c) {
            terrain_.forEachCellInDisc(c, e.radius, [&](int x, int y) {
                if (terrain_.at(x, y) != Cell::Dirt) return;
                terrain_.set(x, y, Cell::Empty);
                events_.dug.push_back(terrain_.cellCentre(x, y));
            });
        });
        break;
    case Tool::Erase:
        // Refund exactly one ink unit per wall cell removed.
        stampAlong(e, [&](Vec2 c) { ink_ += terrain_.erase(c, e.radius); });
        break;
    case Tool::Wall: {
        const int w = terrain_.width();
        stampAlong(e, [&](Vec2 c) {
            terrain_.forEachCellInDisc(c, e.radius, [&](int x, int y) {
                if (ink_ <= 0) return;
                if (terrain_.at(x, y) != Cell::Empty) return;
                if (occupied_[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]) return;
                terrain_.set(x, y, Cell::Wall);
                --ink_;
            });
        });
        break;
    }
    }
}

void World::collectDrainsAndPits() {
    const auto pos = fluid_.positions();
    removeFlags_.assign(pos.size(), 0);
    bool any = false;
    int savedNow = 0;
    for (size_t i = 0; i < pos.size(); ++i) {
        // Drains are checked first, so a particle in both counts as saved.
        // Each particle is removed the moment it is counted, so it can never
        // be counted twice.
        bool counted = false;
        for (const Rect& d : level_.drains) {
            if (d.contains(pos[i])) {
                ++score_.saved;
                ++savedNow;
                counted = true;
                events_.drained.push_back(pos[i]);
                break;
            }
        }
        if (!counted) {
            for (const Rect& pit : level_.pits) {
                if (pit.contains(pos[i])) {
                    ++score_.lost;
                    counted = true;
                    events_.lost.push_back(pos[i]);
                    break;
                }
            }
        }
        removeFlags_[i] = counted ? 1 : 0;
        any |= counted;
    }
    if (any) fluid_.removeMarked(removeFlags_);

    secondsSinceSave_ = savedNow > 0 ? 0.0 : secondsSinceSave_ + config_.stepSeconds;

    // Stall detection (simulated time, so it is deterministic too).
    if (savedNow > 0 || !everEdited_ || fluid_.size() == 0) {
        quietSeconds_ = 0.0;
    } else {
        double meanSpeed = 0.0;
        for (const Vec2 v : fluid_.velocities()) meanSpeed += length(v);
        meanSpeed /= static_cast<double>(fluid_.size());
        quietSeconds_ = meanSpeed < kStillMeanSpeed ? quietSeconds_ + config_.stepSeconds : 0.0;
    }
}

void World::updateStatus() {
    if (status_ == LevelStatus::Playing) {
        const int alive = static_cast<int>(fluid_.size());
        if (score_.saved >= score_.required) status_ = LevelStatus::Won;
        else if (score_.saved + alive < score_.required) status_ = LevelStatus::Lost;
    }
    stalled_ = status_ == LevelStatus::Playing && quietSeconds_ >= kStallSeconds;
}

} // namespace spillway
