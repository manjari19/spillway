#include "spillway/fluid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

#include "spillway/rng.h"
#include "spillway/terrain.h"
#include "spillway/thread_pool.h"

namespace spillway {

FluidSystem::FluidSystem(const FluidParams& params, Vec2 boundsMin, Vec2 boundsMax)
    : params_(params), kernels_(params.smoothingRadius), boundsMin_(boundsMin), boundsMax_(boundsMax) {
    grid_.configure(boundsMin_, boundsMax_, params_.smoothingRadius);
}

void FluidSystem::setParams(const FluidParams& params) {
    params_ = params;
    kernels_ = SmoothingKernels(params.smoothingRadius);
    grid_.configure(boundsMin_, boundsMax_, params_.smoothingRadius);
}

void FluidSystem::addParticle(Vec2 position, Vec2 velocity) {
    positions_.push_back(position);
    velocities_.push_back(velocity);
    predicted_.push_back(position);
    newVelocities_.push_back(velocity);
    densities_.push_back(params_.targetDensity);
    nearDensities_.push_back(0.0f);
}

float FluidSystem::restSpacing() const {
    // With unit mass, a square lattice of spacing s has number density 1/s^2.
    return 1.0f / std::sqrt(params_.targetDensity);
}

void FluidSystem::spawnBlock(Vec2 min, Vec2 max, uint64_t seed) {
    Rng rng(seed);
    const float s = restSpacing();
    const float jitter = 0.05f * s;
    for (float y = min.y + 0.5f * s; y < max.y; y += s) {
        for (float x = min.x + 0.5f * s; x < max.x; x += s) {
            addParticle({x + rng.uniform(-jitter, jitter), y + rng.uniform(-jitter, jitter)});
        }
    }
}

void FluidSystem::setTerrain(const Terrain* terrain) { terrain_ = terrain; }

void FluidSystem::removeMarked(std::span<const uint8_t> remove) {
    size_t out = 0;
    for (size_t i = 0; i < positions_.size(); ++i) {
        if (remove[i]) continue;
        positions_[out] = positions_[i];
        velocities_[out] = velocities_[i];
        predicted_[out] = predicted_[i];
        newVelocities_[out] = newVelocities_[i];
        densities_[out] = densities_[i];
        nearDensities_[out] = nearDensities_[i];
        ++out;
    }
    positions_.resize(out);
    velocities_.resize(out);
    predicted_.resize(out);
    newVelocities_.resize(out);
    densities_.resize(out);
    nearDensities_.resize(out);
}

void FluidSystem::clear() {
    positions_.clear();
    velocities_.clear();
    predicted_.clear();
    newVelocities_.clear();
    densities_.clear();
    nearDensities_.clear();
}

template <class Fn>
void FluidSystem::forEachNeighbourCandidate(Vec2 p, Fn&& fn) const {
    if (search_ == NeighbourSearch::Grid) {
        grid_.forEachCandidate(p, fn);
    } else {
        // Brute force: every particle is a candidate. O(n^2), kept for
        // benchmarking and as a reference in tests.
        const uint32_t n = static_cast<uint32_t>(predicted_.size());
        for (uint32_t j = 0; j < n; ++j) fn(j);
    }
}

template <class Fn>
void FluidSystem::forRange(size_t n, Fn&& fn) {
    if (pool_ && pool_->threads() > 1) pool_->parallelFor(n, std::function<void(size_t, size_t)>(fn));
    else fn(size_t{0}, n);
}

void FluidSystem::step(float dt) {
    const size_t n = size();
    if (n == 0) return;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

    // Every pass below except the grid build reads only the previous pass's
    // arrays and writes only particle i's own entries, so it can be split
    // across threads without locks and without changing any result.
    const auto t0 = Clock::now();
    // 1. Apply gravity / interaction and predict where each particle is going.
    forRange(n, [&](size_t b, size_t e) { predictRange(b, e, dt); });
    const auto t1 = Clock::now();
    // 2. Rebuild the neighbour grid on the predicted positions (a serial
    //    counting sort; a small part of the step), and copy positions into
    //    cell order for the neighbour loops.
    const bool grid = search_ == NeighbourSearch::Grid;
    if (grid) {
        grid_.build(predicted_);
        sorted_.resize(n);
        const auto& order = grid_.sortedIndex();
        forRange(n, [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                sorted_[k].index = order[k];
                sorted_[k].pos = predicted_[order[k]];
            }
        });
    }
    const auto t2 = Clock::now();
    // 3. Density and near-density at predicted positions.
    forRange(n, [&](size_t b, size_t e) { densityRange(b, e); });
    if (grid) {
        // The force pass reads neighbours' densities and velocities too.
        forRange(n, [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                const uint32_t j = sorted_[k].index;
                sorted_[k].density = densities_[j];
                sorted_[k].nearDensity = nearDensities_[j];
                sorted_[k].vel = velocities_[j];
            }
        });
    }
    const auto t3 = Clock::now();
    // 4. Pressure + viscosity -> new velocities (written to a separate buffer
    //    because viscosity reads neighbours' old velocities).
    forRange(n, [&](size_t b, size_t e) { forceRange(b, e, dt); });
    velocities_.swap(newVelocities_);
    const auto t4 = Clock::now();
    // 5. Move particles and collide with the boundary and terrain.
    forRange(n, [&](size_t b, size_t e) { integrateRange(b, e, dt); });
    const auto t5 = Clock::now();

    timings_.grid += ms(t1, t2);
    timings_.density += ms(t2, t3);
    timings_.forces += ms(t3, t4);
    timings_.integrate += ms(t0, t1) + ms(t4, t5);
    ++timings_.steps;
}

Vec2 FluidSystem::externalAcceleration(Vec2 position, Vec2 velocity) const {
    Vec2 gravity = params_.gravity;
    if (!interaction_.active) return gravity;

    const Vec2 offset = interaction_.point - position;
    const float d2 = lengthSq(offset);
    const float r = interaction_.radius;
    if (d2 >= r * r) return gravity;

    // Blend from full gravity at the edge of the brush to an attraction
    // toward its centre. The -velocity term damps particles inside the brush
    // so a pull gathers water into a ball instead of flinging it around.
    const float dist = std::sqrt(d2);
    const float centreT = 1.0f - dist / r;
    const Vec2 dir = dist > 1e-6f ? offset / dist : Vec2{};
    const float gravityWeight = 1.0f - centreT * std::clamp(interaction_.strength / 10.0f, 0.0f, 1.0f);
    return gravity * gravityWeight + dir * (centreT * interaction_.strength) - velocity * centreT;
}

void FluidSystem::predictRange(size_t begin, size_t end, float dt) {
    for (size_t i = begin; i < end; ++i) {
        velocities_[i] += externalAcceleration(positions_[i], velocities_[i]) * dt;
        predicted_[i] = positions_[i] + velocities_[i] * dt;
    }
}

void FluidSystem::densityRange(size_t begin, size_t end) {
    const float h2 = kernels_.radiusSq();
    for (size_t i = begin; i < end; ++i) {
        const Vec2 pi = predicted_[i];
        float density = 0.0f;
        float nearDensity = 0.0f;
        // rho_i = sum_j m_j W(|x_i - x_j|). Mass is 1, and j includes i
        // itself, so density is never zero.
        auto add = [&](Vec2 pj) {
            const float r2 = lengthSq(pj - pi);
            if (r2 >= h2) return;
            const float r = std::sqrt(r2);
            density += kernels_.spikyPow2(r);
            nearDensity += kernels_.spikyPow3(r);
        };
        if (search_ == NeighbourSearch::Grid) {
            grid_.forEachCandidateRange(pi, [&](uint32_t b, uint32_t e) {
                for (uint32_t k = b; k < e; ++k) add(sorted_[k].pos);
            });
        } else {
            forEachNeighbourCandidate(pi, [&](uint32_t j) { add(predicted_[j]); });
        }
        densities_[i] = density;
        nearDensities_[i] = nearDensity;
    }
}

void FluidSystem::forceRange(size_t begin, size_t end, float dt) {
    const float h2 = kernels_.radiusSq();
    const float rho0 = params_.targetDensity;
    const float k = params_.pressureMultiplier;
    const float kNear = params_.nearPressureMultiplier;

    for (size_t i = begin; i < end; ++i) {
        const Vec2 pi = predicted_[i];
        const Vec2 vi = velocities_[i];
        // Equation of state.
        const float pressure = k * (densities_[i] - rho0);
        const float nearPressure = kNear * nearDensities_[i];

        Vec2 pressureForce{};
        Vec2 viscosityForce{};

        auto add = [&](uint32_t j, Vec2 pjPos, Vec2 vj, float rhoJ, float nearJ) {
            if (j == i) return;
            const Vec2 offset = pjPos - pi;
            const float r2 = lengthSq(offset);
            if (r2 >= h2) return;
            const float r = std::sqrt(r2);
            // Two particles exactly on top of each other have no direction;
            // pick one deterministically from the index order (antisymmetric,
            // so i and j are pushed opposite ways).
            const Vec2 dir = r > 1e-6f ? offset / r : Vec2{0.0f, i < j ? 1.0f : -1.0f};

            // Pressure force: -grad P. Using the *average* pressure of i and j
            // makes the force on i from j equal and opposite to the force on j
            // from i (Newton's third law), so momentum is conserved.
            const float pj = k * (rhoJ - rho0);
            const float pjNear = kNear * nearJ;
            const float sharedPressure = 0.5f * (pressure + pj);
            const float sharedNear = 0.5f * (nearPressure + pjNear);
            // The kernel derivative is negative, so positive pressure pushes
            // i away from j (along -dir).
            pressureForce += dir * (kernels_.spikyPow2Derivative(r) * sharedPressure / rhoJ);
            pressureForce += dir * (kernels_.spikyPow3Derivative(r) * sharedNear / nearJ);

            // Viscosity: nudge v_i toward its neighbours' velocities,
            // weighted by a smooth kernel. Damps jitter and gives the fluid
            // a slightly thick, cohesive look.
            viscosityForce += (vj - vi) * kernels_.poly6(r2);
        };
        if (search_ == NeighbourSearch::Grid) {
            grid_.forEachCandidateRange(pi, [&](uint32_t b, uint32_t e) {
                for (uint32_t q = b; q < e; ++q) {
                    const SortedParticle& sp = sorted_[q];
                    add(sp.index, sp.pos, sp.vel, sp.density, sp.nearDensity);
                }
            });
        } else {
            forEachNeighbourCandidate(pi, [&](uint32_t j) {
                add(j, predicted_[j], velocities_[j], densities_[j], nearDensities_[j]);
            });
        }

        // F = m a with "mass density": a = F / rho.
        const Vec2 accel = pressureForce / densities_[i] + viscosityForce * params_.viscosityStrength;
        newVelocities_[i] = vi + accel * dt;
    }
}

void FluidSystem::integrateRange(size_t begin, size_t end, float dt) {
    // Speed limit: the global safety clamp, and with terrain also "at most one
    // cell per substep" so the swept test below never has far to look.
    float maxSpeed = params_.maxSpeed;
    if (terrain_) maxSpeed = std::min(maxSpeed, terrain_->cellSize() / dt);
    const float maxSpeed2 = maxSpeed * maxSpeed;

    for (size_t i = begin; i < end; ++i) {
        Vec2& v = velocities_[i];
        const float s2 = lengthSq(v);
        if (s2 > maxSpeed2) v *= maxSpeed / std::sqrt(s2);

        const Vec2 previous = positions_[i];
        Vec2 p = previous + v * dt;
        if (terrain_) {
            // Anti-tunneling: if the straight path from the old to the new
            // position enters a solid cell, stop just before it. The SDF
            // push-out then places the particle exactly on the surface.
            float tHit;
            if (terrain_->segmentHitsSolid(previous, p, tHit)) {
                const float len = length(p - previous);
                const float back = len > 0.0f ? 1e-4f / len : 0.0f;
                p = previous + (p - previous) * std::max(0.0f, tHit - back);
            }
            resolveTerrain(p, v, previous);
        }
        resolveBounds(p, v);
        positions_[i] = p;
    }
}

void FluidSystem::resolveTerrain(Vec2& p, Vec2& v, Vec2 previous) const {
    const float r = params_.particleRadius;
    // Two projection passes: bilinear phi is not exactly linear, so one push
    // can land a hair short near corners.
    for (int iter = 0; iter < 2; ++iter) {
        const float phi = terrain_->distance(p);
        if (phi >= r) return;
        const Vec2 g = terrain_->gradient(p);
        const float gl = length(g);
        if (gl < 1e-4f) {
            // Degenerate spot (e.g. a gap narrower than a cell, where every
            // nearby node reads 0): there is no usable normal, so undo the
            // move instead of guessing a direction.
            p = previous;
            v = {};
            return;
        }
        const Vec2 n = g / gl;
        // Push out along the normal so the particle sits exactly r from the
        // surface.
        p += n * (r - phi);
        // Remove the velocity going into the surface (no bounce), then apply
        // Coulomb friction: tangential speed drops by mu * (normal speed
        // removed), but never reverses direction.
        const float vn = dot(v, n);
        if (vn < 0.0f) {
            Vec2 vt = v - n * vn;
            const float vtLen = length(vt);
            const float drop = params_.terrainFriction * -vn;
            vt = vtLen > drop ? vt * ((vtLen - drop) / vtLen) : Vec2{};
            v = vt;
        }
    }
}

void FluidSystem::resolveBounds(Vec2& p, Vec2& v) const {
    // Push the particle back inside the box and reflect (and damp) the
    // velocity component going into the wall.
    const float r = params_.particleRadius;
    const float e = params_.boundaryRestitution;
    const Vec2 lo = boundsMin_ + Vec2{r, r};
    const Vec2 hi = boundsMax_ - Vec2{r, r};
    if (p.x < lo.x) { p.x = lo.x; if (v.x < 0.0f) v.x = -v.x * e; }
    if (p.x > hi.x) { p.x = hi.x; if (v.x > 0.0f) v.x = -v.x * e; }
    if (p.y < lo.y) { p.y = lo.y; if (v.y < 0.0f) v.y = -v.y * e; }
    if (p.y > hi.y) { p.y = hi.y; if (v.y > 0.0f) v.y = -v.y * e; }
}

float FluidSystem::sampleDensity(Vec2 point) const {
    float density = 0.0f;
    const float h2 = kernels_.radiusSq();
    for (const Vec2& p : positions_) {
        const float r2 = lengthSq(p - point);
        if (r2 < h2) density += kernels_.spikyPow2(std::sqrt(r2));
    }
    return density;
}

} // namespace spillway
