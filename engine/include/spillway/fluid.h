#pragma once

// 2D SPH fluid.
//
// The model is "predictive" SPH with a near-pressure term:
//   - density and pressure as in Mueller et al. 2003 (equation of state
//     P = k * (rho - rho0), so particles push apart when too dense and pull
//     together slightly when too sparse, which gives some surface tension),
//   - near-density / near-pressure from Clavet et al. 2005, a short-range
//     repulsion that stops particles clumping and keeps the surface smooth,
//   - simple neighbour-averaging viscosity,
//   - densities are evaluated at *predicted* positions (x + v*dt), which
//     makes the pressure response anticipate compression and lets us take
//     game-sized timesteps without exploding.
//
// Every pass is a "gather": particle i reads its neighbours' data from the
// previous pass and writes only its own result. There are no scattered writes,
// so the passes can run on many threads with no locks and give identical
// results to the single-threaded run.

#include <cstdint>
#include <span>
#include <vector>

#include "spillway/kernels.h"
#include "spillway/spatial_grid.h"
#include "spillway/vec2.h"

namespace spillway {

struct FluidParams {
    float smoothingRadius = 0.35f;       // h, metres
    float targetDensity = 55.0f;         // rho0, particles per m^2 (mass = 1)
    float pressureMultiplier = 500.0f;   // stiffness k
    float nearPressureMultiplier = 18.0f;
    float viscosityStrength = 0.06f;
    Vec2 gravity{0.0f, 12.0f};           // +y is down
    float particleRadius = 0.05f;        // collision radius against walls
    float boundaryRestitution = 0.3f;    // fraction of normal speed kept on bounce (box)
    float terrainFriction = 0.1f;        // Coulomb coefficient against terrain
    float maxSpeed = 25.0f;              // safety clamp, m/s
};

class Terrain;

// Same fluid, different particle count (the game's water-quality setting).
// Rest density is a particle *number* density, so scaling it by m packs m
// times more particles into the same volume; pressure forces stay the same
// size (both P and the neighbour sum scale by m, and a = F / rho), while the
// viscosity sum gains m times more neighbours, so its strength is divided by m.
inline FluidParams withParticleScale(FluidParams p, float m) {
    p.targetDensity *= m;
    p.viscosityStrength /= m;
    return p;
}

enum class NeighbourSearch { Grid, BruteForce };

class ThreadPool;

// Time spent in each phase of step(), summed over the steps since the last
// reset (milliseconds). "integrate" includes the prediction pass and all
// collision handling.
struct StepTimings {
    double grid = 0.0, density = 0.0, forces = 0.0, integrate = 0.0;
    int steps = 0;
    double total() const { return grid + density + forces + integrate; }
};

// Mouse-style interaction: pulls particles toward (strength > 0) or pushes them
// away from (strength < 0) a point.
struct Interaction {
    Vec2 point{};
    float radius = 1.5f;
    float strength = 0.0f;
    bool active = false;
};

class FluidSystem {
public:
    FluidSystem(const FluidParams& params, Vec2 boundsMin, Vec2 boundsMax);

    void addParticle(Vec2 position, Vec2 velocity = {});
    // Fills a rectangle with particles at rest-density spacing. A tiny seeded
    // jitter breaks the perfect lattice (which otherwise stacks in columns).
    void spawnBlock(Vec2 min, Vec2 max, uint64_t seed = 1);
    void clear();

    // Removes every particle i with remove[i] != 0, keeping the order of the
    // rest (stable compaction, so results stay deterministic).
    void removeMarked(std::span<const uint8_t> remove);

    // Collide against level geometry. The terrain must outlive this system.
    // Also limits per-substep displacement to one terrain cell.
    void setTerrain(const Terrain* terrain);
    const Terrain* terrain() const { return terrain_; }

    // Advance by one substep of length dt seconds.
    void step(float dt);

    // Spacing at which a square lattice has exactly the target density.
    float restSpacing() const;

    size_t size() const { return positions_.size(); }
    std::span<const Vec2> positions() const { return positions_; }
    std::span<const Vec2> velocities() const { return velocities_; }
    std::span<const float> densities() const { return densities_; }

    const FluidParams& params() const { return params_; }
    void setParams(const FluidParams& params);
    void setGravity(Vec2 g) { params_.gravity = g; }
    const SmoothingKernels& kernels() const { return kernels_; }

    NeighbourSearch neighbourSearch() const { return search_; }
    void setNeighbourSearch(NeighbourSearch mode) { search_ = mode; }

    void setInteraction(const Interaction& interaction) { interaction_ = interaction; }

    // Run the per-particle passes on a thread pool (nullptr = this thread
    // only). The pool must outlive this system. Results are bit-identical
    // for any thread count: see thread_pool.h.
    void setThreadPool(ThreadPool* pool) { pool_ = pool; }
    ThreadPool* threadPool() const { return pool_; }

    const StepTimings& timings() const { return timings_; }
    void resetTimings() { timings_ = {}; }

    // Density at an arbitrary point using current positions (tests, debug).
    float sampleDensity(Vec2 point) const;

    const SpatialGrid& grid() const { return grid_; }
    Vec2 boundsMin() const { return boundsMin_; }
    Vec2 boundsMax() const { return boundsMax_; }

private:
    // Each pass works on a range [begin, end) so it can later be split across
    // threads without changing the code inside.
    void predictRange(size_t begin, size_t end, float dt);
    void densityRange(size_t begin, size_t end);
    void forceRange(size_t begin, size_t end, float dt);
    void integrateRange(size_t begin, size_t end, float dt);

    Vec2 externalAcceleration(Vec2 position, Vec2 velocity) const;
    void resolveBounds(Vec2& position, Vec2& velocity) const;
    void resolveTerrain(Vec2& position, Vec2& velocity, Vec2 previous) const;

    template <class Fn>
    void forEachNeighbourCandidate(Vec2 p, Fn&& fn) const;
    // Runs fn(begin, end) over [0, n), split across the pool if there is one.
    template <class Fn>
    void forRange(size_t n, Fn&& fn);

    FluidParams params_;
    SmoothingKernels kernels_;
    Vec2 boundsMin_;
    Vec2 boundsMax_;
    NeighbourSearch search_ = NeighbourSearch::Grid;
    Interaction interaction_{};
    SpatialGrid grid_;
    const Terrain* terrain_ = nullptr;
    ThreadPool* pool_ = nullptr;
    StepTimings timings_;

    // Structure-of-arrays particle storage.
    std::vector<Vec2> positions_;
    std::vector<Vec2> velocities_;
    std::vector<Vec2> predicted_;
    std::vector<Vec2> newVelocities_;
    std::vector<float> densities_;
    std::vector<float> nearDensities_;

    // Grid mode: copies of what the neighbour loops read, in cell order, so
    // each 3x3 neighbour row is one contiguous read instead of a gather
    // through the grid's index. Same values and same order as reading
    // through the index, so results are unchanged (see README, Data layout).
    struct SortedParticle {
        Vec2 pos;          // predicted position
        Vec2 vel;          // velocity before the force pass
        float density;
        float nearDensity;
        uint32_t index;    // particle index
    };
    std::vector<SortedParticle> sorted_;
};

} // namespace spillway
