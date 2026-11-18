#pragma once

namespace spillway {

// Fixed-timestep accumulator ("Fix Your Timestep", Glenn Fiedler).
//
// Rendering runs at whatever rate the display allows, but physics always
// advances in identical fixed steps. Each frame we add the real elapsed time
// to an accumulator and run as many whole steps as fit. This makes the
// simulation independent of frame rate and is a prerequisite for
// determinism: the same inputs give the same sequence of steps.
class FixedTimestep {
public:
    explicit FixedTimestep(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 4);

    // Adds a frame's elapsed time and returns how many fixed steps to run.
    // If we fall too far behind (slow machine, debugger pause) the backlog is
    // dropped instead of trying to catch up, which would make the next frame
    // even slower ("spiral of death").
    int advance(double frameSeconds);

    // How far we are into the next step, in [0, 1). Usable for interpolation.
    double alpha() const { return accumulator_ / step_; }
    double stepSeconds() const { return step_; }
    void reset() { accumulator_ = 0.0; }

private:
    double step_;
    int maxSteps_;
    double accumulator_ = 0.0;
};

} // namespace spillway
