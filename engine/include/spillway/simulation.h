#pragma once

#include <cstdint>

#include "spillway/fluid.h"
#include "spillway/timestep.h"

namespace spillway {

struct SimulationConfig {
    double stepSeconds = 1.0 / 60.0; // one fixed step
    int substeps = 4;                // fluid substeps per fixed step
    int maxStepsPerFrame = 4;
};

// Owns the fluid and drives it with a fixed timestep.
class Simulation {
public:
    Simulation(const FluidParams& params, Vec2 boundsMin, Vec2 boundsMax,
               const SimulationConfig& config = {});

    // Call once per rendered frame with the real frame time. Returns the
    // number of fixed steps that ran.
    int update(double frameSeconds);

    // Run exactly one fixed step (= `substeps` fluid substeps).
    void step();

    FluidSystem& fluid() { return fluid_; }
    const FluidSystem& fluid() const { return fluid_; }
    const SimulationConfig& config() const { return config_; }

    uint64_t stepCount() const { return stepCount_; }
    double lastStepMs() const { return lastStepMs_; }

private:
    SimulationConfig config_;
    FluidSystem fluid_;
    FixedTimestep timestep_;
    uint64_t stepCount_ = 0;
    double lastStepMs_ = 0.0;
};

} // namespace spillway
