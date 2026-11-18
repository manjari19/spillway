#include "spillway/timestep.h"

#include <algorithm>

namespace spillway {

FixedTimestep::FixedTimestep(double stepSeconds, int maxStepsPerFrame)
    : step_(stepSeconds), maxSteps_(maxStepsPerFrame) {}

int FixedTimestep::advance(double frameSeconds) {
    // Ignore negative/huge frame times (e.g. window dragged, breakpoint hit).
    accumulator_ += std::clamp(frameSeconds, 0.0, 0.25);

    // A tiny epsilon so that e.g. 60 frames of exactly 1/60 s produce exactly
    // 60 steps despite floating-point rounding in the accumulator.
    constexpr double eps = 1e-9;
    int steps = 0;
    while (accumulator_ + eps >= step_ && steps < maxSteps_) {
        accumulator_ -= step_;
        ++steps;
    }
    if (accumulator_ + eps >= step_) accumulator_ = 0.0; // drop the backlog
    accumulator_ = std::max(accumulator_, 0.0);
    return steps;
}

} // namespace spillway
