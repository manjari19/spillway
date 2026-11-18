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
    float boundaryRestitution = 0.3f;    // fraction of normal speed kept on bounce
    float maxSpeed = 25.0f;              // safety clamp, m/s
};

enum class NeighbourSearch { Grid, BruteForce };

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
    const SmoothingKernels& kernels() const { return kernels_; }

    NeighbourSearch neighbourSearch() const { return search_; }
    void setNeighbourSearch(NeighbourSearch mode) { search_ = mode; }

    void setInteraction(const Interaction& interaction) { interaction_ = interaction; }

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

    template <class Fn>
    void forEachNeighbourCandidate(Vec2 p, Fn&& fn) const;

    FluidParams params_;
    SmoothingKernels kernels_;
    Vec2 boundsMin_;
    Vec2 boundsMax_;
    NeighbourSearch search_ = NeighbourSearch::Grid;
    Interaction interaction_{};
    SpatialGrid grid_;

    // Structure-of-arrays particle storage.
    std::vector<Vec2> positions_;
    std::vector<Vec2> velocities_;
    std::vector<Vec2> predicted_;
    std::vector<Vec2> newVelocities_;
    std::vector<float> densities_;
    std::vector<float> nearDensities_;
};

} // namespace spillway
