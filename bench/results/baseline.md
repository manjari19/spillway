# Single-threaded baseline

Recorded before any optimisation (threading off, neighbour data read through the grid index; see data_layout.md for the layout change that followed).

| | |
|---|---|
| CPU | Apple M5 (4 performance + 6 efficiency cores) |
| Threads used for "all cores" | 10 |
| Compiler | Clang 21.0.0 (clang-2100.1.1.101) |
| Build type | Release |
| Date | 2026-11-18 |

Dam-break box scene, uniform-grid neighbour search, 1 thread. 600 steps x 5 repetitions; a step is one 1/60 s frame (4 substeps). Phase times are per step.

| Particles | Mean step (ms) | p95 step (ms) | Grid build | Density | Forces | Integrate + collision |
|---:|---:|---:|---:|---:|---:|---:|
| 5,041 | 8.106 | 9.120 | 0.033 ms (0%) | 3.748 ms (46%) | 4.285 ms (53%) | 0.040 ms (0%) |
| 19,881 | 38.45 | 50.64 | 0.140 ms (0%) | 17.730 ms (46%) | 20.423 ms (53%) | 0.153 ms (0%) |
