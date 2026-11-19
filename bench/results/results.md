# Benchmark results

| | |
|---|---|
| CPU | Apple M5 (4 performance + 6 efficiency cores) |
| Threads used for "all cores" | 10 |
| Compiler | Clang 21.0.0 (clang-2100.1.1.101) |
| Build type | Release |
| Date | 2026-11-18 |

Scene: dam break in a box (a square block of water collapsing). A step is one 1/60 s game frame of simulation (4 substeps). Each configuration ran 600 steps x 5 repetitions from a fresh scene; mean and p95 are over all 3000 step times. The last two columns are speedups relative to the 1-thread grid (mean step time of the 1-thread grid divided by the mean of the other mode; below 1x means slower).

| Particles | Brute force, 1 thread (mean / p95 ms) | Grid, 1 thread (mean / p95 ms) | Grid, 10 threads (mean / p95 ms) | Brute vs grid 1 thread | Grid 10 threads |
|---:|---:|---:|---:|---:|---:|
| 1,024 | 4.177 / 4.317 | 1.194 / 1.315 | 0.905 / 1.283 | 0.29x | 1.32x |
| 2,500 | 24.41 / 26.78 | 4.052 / 5.127 | 2.019 / 2.444 | 0.17x | 2.01x |
| 5,041 | skipped | 8.507 / 9.933 | 3.494 / 4.373 | n/a | 2.43x |
| 10,000 | skipped | 17.92 / 21.51 | 6.504 / 8.072 | n/a | 2.75x |
| 19,881 | skipped | 39.65 / 52.24 | 14.04 / 20.63 | n/a | 2.82x |

Skipped brute-force runs: a single probe step was timed first, and the run was skipped when 3000 steps at that speed would take more than 120 s. Probe steps: 5,041 particles took 129.41 ms, 10,000 particles took 499.73 ms, 19,881 particles took 1508.47 ms. These probes are single measurements, not benchmark results.

## Largest particle count within a 60 FPS frame

Largest count whose p95 step time stayed at or under 16.67 ms, measured over 200 steps x 3 repetitions per candidate. Candidates were chosen by bisection on the requested count in steps of 500; the scene rounds each count to a square block of particles, so the actual counts differ slightly. Candidates clearly over budget (mean above 1.5x the budget after 20 steps) were rejected without a full run.

| Threads | Particles | Mean step (ms) | p95 step (ms) |
|---:|---:|---:|---:|
| 1 | 7,921 | 12.41 | 15.67 |
| 10 | 20,449 | 11.42 | 15.96 |
