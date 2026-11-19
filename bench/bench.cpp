// Headless benchmark for the SPH solver.
//
// Scene: a dam break. A square block of water (side H) starts against the
// left wall of a box 2.5H wide and 1.25H tall and collapses across it. H is
// chosen so the block holds the requested number of particles.
//
// A "step" here is one game frame of simulation: 1/60 s, done as 4 substeps
// of 1/240 s, exactly what the game runs per frame. So "step time under
// 16.6 ms" means the simulation alone fits a 60 FPS frame.
//
// Every number written to the results is measured in this run. Nothing is
// extrapolated: a configuration that would take too long is skipped and
// reported as skipped.
//
// Usage: spillway_bench [--quick] [--baseline] [--out DIR]
//   --quick     small counts and few steps (CI smoke test)
//   --baseline  single-threaded phase breakdown at 5,000 and 20,000 only
//   --out DIR   where results.md / results.csv go (default: bench/results)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "spillway/fluid.h"
#include "spillway/thread_pool.h"

using namespace spillway;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kSubsteps = 4;
constexpr float kSubstepDt = 1.0f / 240.0f;
constexpr double kFrameBudgetMs = 1000.0 / 60.0;

struct Mode {
    const char* name; // for tables
    const char* key;  // for CSV
    NeighbourSearch search;
    bool allCores;
};

struct Result {
    int requested = 0, particles = 0, threads = 1, steps = 0, reps = 0;
    double meanMs = 0, p95Ms = 0;
    StepTimings phases; // summed over all measured substeps
    bool skipped = false;
    double probeMs = 0; // brute force: the single probe step that decided a skip
};

// Builds the dam-break scene for about `n` particles.
FluidSystem makeScene(int n) {
    const float rho0 = FluidParams{}.targetDensity;
    const float h = std::sqrt(static_cast<float>(n) / rho0);
    FluidSystem f(FluidParams{}, {0.0f, 0.0f}, {2.5f * h, 1.25f * h});
    f.spawnBlock({0.0f, 0.25f * h}, {h, 1.25f * h}, 42);
    return f;
}

double stepFrame(FluidSystem& f) {
    const auto t0 = Clock::now();
    for (int s = 0; s < kSubsteps; ++s) f.step(kSubstepDt);
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    // Nearest-rank percentile.
    const size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
    return v[std::min(v.size() - 1, rank == 0 ? 0 : rank - 1)];
}

// Runs `reps` independent repetitions of `steps` frames from a fresh scene.
Result run(int n, const Mode& m, ThreadPool* pool, int steps, int reps) {
    Result r;
    r.requested = n;
    r.steps = steps;
    r.reps = reps;
    r.threads = pool ? pool->threads() : 1;
    std::vector<double> frames;
    frames.reserve(static_cast<size_t>(steps * reps));
    for (int rep = 0; rep < reps; ++rep) {
        FluidSystem f = makeScene(n);
        f.setNeighbourSearch(m.search);
        f.setThreadPool(pool);
        r.particles = static_cast<int>(f.size());
        for (int i = 0; i < steps; ++i) frames.push_back(stepFrame(f));
        r.phases.grid += f.timings().grid;
        r.phases.density += f.timings().density;
        r.phases.forces += f.timings().forces;
        r.phases.integrate += f.timings().integrate;
        r.phases.steps += f.timings().steps;
    }
    r.meanMs = std::accumulate(frames.begin(), frames.end(), 0.0) / static_cast<double>(frames.size());
    r.p95Ms = percentile(frames, 95.0);
    return r;
}

std::string cpuName() {
#if defined(__APPLE__)
    char buf[256] = {};
    size_t len = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return buf;
#elif defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("model name", 0) == 0) return line.substr(line.find(':') + 2);
#endif
    return "unknown CPU";
}

std::string coreDetail() {
#if defined(__APPLE__)
    int p = 0, e = 0;
    size_t len = sizeof p;
    if (sysctlbyname("hw.perflevel0.physicalcpu", &p, &len, nullptr, 0) == 0) {
        len = sizeof e;
        sysctlbyname("hw.perflevel1.physicalcpu", &e, &len, nullptr, 0);
        if (e > 0) return std::to_string(p) + " performance + " + std::to_string(e) + " efficiency cores";
        return std::to_string(p) + " cores";
    }
#endif
    return std::to_string(ThreadPool::hardwareThreads()) + " hardware threads";
}

std::string compilerName() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

std::string fmtMs(double v) {
    char b[32];
    std::snprintf(b, sizeof b, v < 10.0 ? "%.3f" : "%.2f", v);
    return b;
}

std::string fmtCount(int v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}

// Largest particle count whose p95 frame time stays within the 60 FPS budget,
// found by bisection to `resolution` particles.
struct MaxResult {
    int particles = 0;
    double p95Ms = 0, meanMs = 0;
    int steps = 0, reps = 0;
};
MaxResult maxUnderBudget(ThreadPool* pool, int lo, int hi, int resolution, int steps, int reps) {
    const Mode grid{"Grid", "grid", NeighbourSearch::Grid, pool != nullptr};
    MaxResult best;
    best.steps = steps;
    best.reps = reps;
    while (hi - lo > resolution) {
        const int mid = (lo + hi) / 2 / resolution * resolution;
        // A candidate that is clearly over budget (mean above 1.5x after its
        // first 20 steps) is rejected without the full run; only candidates
        // that pass are measured in full, and only those numbers are reported.
        {
            FluidSystem probe = makeScene(mid);
            probe.setThreadPool(pool);
            double sum = 0;
            for (int i = 0; i < 20; ++i) sum += stepFrame(probe);
            if (sum / 20.0 > 1.5 * kFrameBudgetMs) {
                std::printf("    %7d particles: over budget after 20 steps (mean %.1f ms)\n", static_cast<int>(probe.size()), sum / 20.0);
                std::fflush(stdout);
                hi = mid;
                continue;
            }
        }
        const Result r = run(mid, grid, pool, steps, reps);
        std::printf("    %7d particles: mean %.2f ms, p95 %.2f ms\n", r.particles, r.meanMs, r.p95Ms);
        std::fflush(stdout);
        if (r.p95Ms <= kFrameBudgetMs) {
            lo = mid;
            best.particles = r.particles;
            best.p95Ms = r.p95Ms;
            best.meanMs = r.meanMs;
        } else {
            hi = mid;
        }
    }
    return best;
}

} // namespace

int main(int argc, char** argv) {
    bool quick = false, baseline = false;
    std::string out = SPILLWAY_BENCH_RESULTS;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) quick = true;
        else if (!std::strcmp(argv[i], "--baseline")) baseline = true;
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else {
            std::fprintf(stderr, "usage: %s [--quick] [--baseline] [--out DIR]\n", argv[0]);
            return 2;
        }
    }
    std::filesystem::create_directories(out);

    const int cores = ThreadPool::hardwareThreads();
    ThreadPool pool(cores);
    std::time_t now = std::time(nullptr);
    char date[32];
    std::strftime(date, sizeof date, "%Y-%m-%d", std::localtime(&now));

    std::ostringstream info;
    info << "| | |\n|---|---|\n";
    info << "| CPU | " << cpuName() << " (" << coreDetail() << ") |\n";
    info << "| Threads used for \"all cores\" | " << cores << " |\n";
    info << "| Compiler | " << compilerName() << " |\n";
    info << "| Build type | " << SPILLWAY_BUILD_TYPE << " |\n";
    info << "| Date | " << date << " |\n";
    std::printf("Spillway benchmark\n%s\n", info.str().c_str());

    if (baseline) {
        // Phase profile of the single-threaded grid solver.
        const Mode grid1{"Grid, 1 thread", "grid_1t", NeighbourSearch::Grid, false};
        std::ofstream md(out + "/baseline.md");
        md << "# Single-threaded baseline\n\n" << info.str()
           << "\nDam-break box scene, uniform-grid neighbour search, 1 thread. 600 steps x 5 repetitions; a step is one "
              "1/60 s frame (4 substeps). Phase times are per step.\n\n"
           << "| Particles | Mean step (ms) | p95 step (ms) | Grid build | Density | Forces | Integrate + collision |\n"
           << "|---:|---:|---:|---:|---:|---:|---:|\n";
        for (int n : {5000, 20000}) {
            const Result r = run(n, grid1, nullptr, 600, 5);
            const double perStep = static_cast<double>(r.steps * r.reps);
            const double total = r.phases.total();
            auto cell = [&](double v) {
                char b[64];
                std::snprintf(b, sizeof b, "%.3f ms (%.0f%%)", v / perStep, 100.0 * v / total);
                return std::string(b);
            };
            md << "| " << fmtCount(r.particles) << " | " << fmtMs(r.meanMs) << " | " << fmtMs(r.p95Ms) << " | "
               << cell(r.phases.grid) << " | " << cell(r.phases.density) << " | " << cell(r.phases.forces) << " | "
               << cell(r.phases.integrate) << " |\n";
            std::printf("%d particles: mean %.3f ms, p95 %.3f ms\n", r.particles, r.meanMs, r.p95Ms);
        }
        std::printf("Wrote %s/baseline.md\n", out.c_str());
        return 0;
    }

    const std::vector<int> counts = quick ? std::vector<int>{1000, 2500} : std::vector<int>{1000, 2500, 5000, 10000, 20000};
    const int steps = quick ? 60 : 600, reps = quick ? 2 : 5;
    const double bruteLimitS = quick ? 10.0 : 120.0;
    const Mode modes[3] = {{"Brute force, 1 thread", "brute_1t", NeighbourSearch::BruteForce, false},
                           {"Grid, 1 thread", "grid_1t", NeighbourSearch::Grid, false},
                           {"Grid, all cores", "grid_all", NeighbourSearch::Grid, true}};

    std::vector<std::vector<Result>> results(counts.size());
    for (size_t c = 0; c < counts.size(); ++c) {
        for (const Mode& m : modes) {
            std::printf("%-22s %6d particles ... ", m.name, counts[c]);
            std::fflush(stdout);
            Result r;
            if (m.search == NeighbourSearch::BruteForce) {
                // One probe step decides whether the full run fits the limit.
                FluidSystem probe = makeScene(counts[c]);
                probe.setNeighbourSearch(NeighbourSearch::BruteForce);
                const double probeMs = stepFrame(probe);
                if (probeMs * steps * reps / 1000.0 > bruteLimitS) {
                    r.requested = counts[c];
                    r.particles = static_cast<int>(probe.size());
                    r.skipped = true;
                    r.probeMs = probeMs;
                    r.threads = 1;
                    std::printf("skipped (one probe step took %.1f ms)\n", probeMs);
                    results[c].push_back(r);
                    continue;
                }
            }
            r = run(counts[c], m, m.allCores ? &pool : nullptr, steps, reps);
            std::printf("mean %.3f ms, p95 %.3f ms\n", r.meanMs, r.p95Ms);
            results[c].push_back(r);
        }
    }

    std::optional<MaxResult> max1, maxAll;
    if (!quick) {
        std::printf("Largest particle count with p95 step <= %.1f ms, 1 thread:\n", kFrameBudgetMs);
        max1 = maxUnderBudget(nullptr, 1000, 64000, 500, 200, 3);
        std::printf("Largest particle count with p95 step <= %.1f ms, %d threads:\n", kFrameBudgetMs, cores);
        maxAll = maxUnderBudget(&pool, 1000, 256000, 500, 200, 3);
    }

    // CSV: one row per measured (or skipped) configuration.
    std::ofstream csv(out + "/results.csv");
    csv << "particles,mode,threads,steps,reps,mean_ms,p95_ms,speedup_vs_grid_1t,skipped\n";
    for (size_t c = 0; c < counts.size(); ++c) {
        const Result& base = results[c][1];
        for (size_t k = 0; k < 3; ++k) {
            const Result& r = results[c][k];
            csv << r.particles << "," << modes[k].key << "," << r.threads << "," << (r.skipped ? 0 : r.steps) << ","
                << (r.skipped ? 0 : r.reps) << ",";
            if (r.skipped) csv << ",,,1\n";
            else csv << fmtMs(r.meanMs) << "," << fmtMs(r.p95Ms) << "," << fmtMs(base.meanMs / r.meanMs) << ",0\n";
        }
    }

    std::ofstream md(out + "/results.md");
    md << "# Benchmark results\n\n" << info.str() << "\n";
    md << "Scene: dam break in a box (a square block of water collapsing). A step is one 1/60 s game frame of "
          "simulation (4 substeps). Each configuration ran " << steps << " steps x " << reps
       << " repetitions from a fresh scene; mean and p95 are over all " << steps * reps
       << " step times. The last two columns are speedups relative to the 1-thread grid (mean step time of the 1-thread grid divided by the mean of the other mode; below 1x means slower).\n\n";
    md << "| Particles | Brute force, 1 thread (mean / p95 ms) | Grid, 1 thread (mean / p95 ms) | Grid, " << cores
       << " threads (mean / p95 ms) | Brute vs grid 1 thread | Grid " << cores << " threads vs grid 1 thread |\n";
    md << "|---:|---:|---:|---:|---:|---:|\n";
    for (size_t c = 0; c < counts.size(); ++c) {
        const Result& b = results[c][0];
        const Result& g = results[c][1];
        const Result& a = results[c][2];
        char sb[32], sa[32];
        std::snprintf(sb, sizeof sb, "%.2fx", g.meanMs / b.meanMs);
        std::snprintf(sa, sizeof sa, "%.2fx", g.meanMs / a.meanMs);
        md << "| " << fmtCount(g.particles) << " | "
           << (b.skipped ? std::string("skipped") : fmtMs(b.meanMs) + " / " + fmtMs(b.p95Ms)) << " | "
           << fmtMs(g.meanMs) << " / " << fmtMs(g.p95Ms) << " | " << fmtMs(a.meanMs) << " / " << fmtMs(a.p95Ms) << " | "
           << (b.skipped ? std::string("n/a") : std::string(sb)) << " | " << sa << " |\n";
    }
    bool anySkipped = false;
    for (size_t c = 0; c < counts.size(); ++c) anySkipped |= results[c][0].skipped;
    if (anySkipped) {
        md << "\nSkipped brute-force runs: a single probe step was timed first, and the run was skipped when "
           << steps * reps << " steps at that speed would take more than " << static_cast<int>(bruteLimitS)
           << " s. Probe steps: ";
        bool first = true;
        for (size_t c = 0; c < counts.size(); ++c)
            if (results[c][0].skipped) {
                md << (first ? "" : ", ") << fmtCount(results[c][0].particles) << " particles took "
                   << fmtMs(results[c][0].probeMs) << " ms";
                first = false;
            }
        md << ". These probes are single measurements, not benchmark results.\n";
    }
    if (max1 && maxAll) {
        md << "\n## Largest particle count within a 60 FPS frame\n\n"
           << "Largest count whose p95 step time stayed at or under "
           << fmtMs(kFrameBudgetMs) << " ms, measured over " << max1->steps << " steps x " << max1->reps
           << " repetitions per candidate. Candidates were chosen by bisection on the requested count in steps of 500; the scene rounds each count to a square block of particles, so the actual counts differ slightly. Candidates clearly over budget (mean above 1.5x the budget after 20 steps) were rejected without a full run.\n\n"
           << "| Threads | Particles | Mean step (ms) | p95 step (ms) |\n|---:|---:|---:|---:|\n"
           << "| 1 | " << fmtCount(max1->particles) << " | " << fmtMs(max1->meanMs) << " | " << fmtMs(max1->p95Ms) << " |\n"
           << "| " << cores << " | " << fmtCount(maxAll->particles) << " | " << fmtMs(maxAll->meanMs) << " | "
           << fmtMs(maxAll->p95Ms) << " |\n";
    }
    std::printf("Wrote %s/results.md and %s/results.csv\n", out.c_str(), out.c_str());
    return 0;
}
