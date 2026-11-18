#include "spillway/simulation.h"

#include <chrono>

namespace spillway {

Simulation::Simulation(const FluidParams& params, Vec2 boundsMin, Vec2 boundsMax,
                       const SimulationConfig& config)
    : config_(config),
      fluid_(params, boundsMin, boundsMax),
      timestep_(config.stepSeconds, config.maxStepsPerFrame) {}

int Simulation::update(double frameSeconds) {
    const int steps = timestep_.advance(frameSeconds);
    for (int s = 0; s < steps; ++s) step();
    return steps;
}

void Simulation::step() {
    const auto t0 = std::chrono::steady_clock::now();
    const float dt = static_cast<float>(config_.stepSeconds / config_.substeps);
    for (int s = 0; s < config_.substeps; ++s) fluid_.step(dt);
    const auto t1 = std::chrono::steady_clock::now();
    lastStepMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ++stepCount_;
}

} // namespace spillway
