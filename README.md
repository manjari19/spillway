# Spillway

A 2D fluid puzzle game running on an SPH water simulation I wrote from scratch in C++20: dig channels, draw walls and guide the water into the drains.

![Spillway gameplay](docs/demo.gif)

There are no physics or fluid libraries. The SPH solver, neighbour search, terrain collision, threading and level tests are all in this repo. [raylib](https://www.raylib.com/) is used only to open a window, read input and draw (including the shaders).

## Build and run

You need CMake 3.20+ and a C++20 compiler (Clang 15+, GCC 12+ or MSVC 2022). raylib and Catch2 are downloaded by CMake on the first configure.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/game/spillway
```

On Linux, raylib needs the X11/OpenGL headers first:
`sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.

Tests and the benchmark:

```sh
ctest --test-dir build            # unit tests, all 10 level solutions, threading determinism
./build/bench/spillway_bench      # full benchmark, writes bench/results/ (takes a while)
./build/bench/spillway_bench --quick
```

Progress and settings are saved in `~/Library/Application Support/Spillway/` on macOS, `%APPDATA%\Spillway\` on Windows and `~/.local/share/Spillway/` on Linux.

## Controls

| | |
|---|---|
| Left mouse (drag) | use the current tool |
| `1` `2` `3` | dig, wall, erase |
| Mouse wheel or `[` `]` | brush size |
| `Enter` | finish once the goal is reached (or wait for the water to settle) |
| `R` | restart the level |
| `Esc` | pause (resume, restart, level path, main menu) / back |
| `Alt+Enter`, or `Ctrl+Cmd+F` on macOS | toggle fullscreen |
| `` ` `` | debug overlay: step time per phase, render time, particle count |
| `G` / `B` / `T` | (debug) show the grid / brute-force neighbour search / threads on or off |

Walls cost ink. Three stars need a high enough save percentage and, on levels with ink, at most half of it used.

## How it works

**SPH step.** A level holds between about 190 and 480 water particles (at the default water quality), each with mass 1. One frame (1/60 s) is four fixed substeps of 1/240 s, so the simulation runs the same at any frame rate. A substep does five passes:

1. Apply gravity and predict each particle's position (`x + v·dt`).
2. Rebuild the neighbour grid on the predicted positions.
3. Density and near-density at the predicted positions (spiky kernels, radius h = 0.35 m).
4. Pressure from `P = k(ρ − ρ₀)`, plus a short-range near-pressure (Clavet et al. 2005) that keeps particles from clumping, plus viscosity. Pressure uses the average of both particles' pressures, so forces are equal and opposite.
5. Integrate, then collide with the terrain and the level bounds.

Evaluating densities at predicted positions makes the pressure react before particles actually overlap, which is what lets the game use a large-ish timestep without the water exploding.

**Spatial hash grid.** Levels have fixed bounds, so the "hash" is a uniform grid with cell size h: any neighbour within h is in the 3×3 block of cells around a particle. The grid is rebuilt every substep with a counting sort (count per cell, prefix sum, scatter in particle order), which is O(n) and leaves each cell's particles in ascending index order. The neighbour loops then read cell-ordered copies of positions, densities and velocities, so each row of the 3×3 block is one contiguous read. Brute force (every pair) is still there behind a switch, for the benchmark and as a test reference.

**Deterministic threading.** Every pass except the grid build is a gather: particle *i* reads the previous pass's arrays and writes only its own output. So the passes split across threads with no locks and no atomics on the data. A small persistent thread pool (`std::thread`, no OpenMP) cuts each pass into fixed contiguous chunks and hands them out with an atomic counter, so the faster cores take more chunks. Each particle's sum is computed by one thread in the same neighbour order no matter how the chunks are distributed, so the result is bit-identical for any thread count. A test runs a dam break and a full level replay on 1, 2, 4 and all hardware threads and compares the state byte for byte.

**Terrain and collision.** Terrain is a grid of 7.5 cm cells (empty, dirt, rock, or player wall) with a signed distance field stored at the cell corners: exact distance to the nearest solid cell within a narrow band, bilinearly interpolated, negative inside. Collision pushes a particle out along the SDF gradient to exactly its radius from the surface, removes the velocity into the surface and applies Coulomb friction. To stop fast particles tunnelling through thin walls, each move is capped at one cell per substep and checked with a grid traversal (Amanatides & Woo) along the path. Digging or drawing only recomputes the SDF in a small dirty rectangle around the edit.

**Marching squares.** The terrain's outline is drawn from the same cell grid the physics uses. Occupancy is blurred slightly for smooth curves, then clamped back so the drawn edge never strays from the real collision boundary by more than a fraction of a cell. Only 16×16-cell chunks touched by an edit are re-meshed.

**Rendering.** Water is a metaball pass: every particle splats a smooth kernel into a 16-bit float target, and a shader thresholds that field into one surface, coloured from violet (slow) to cyan (fast), with a lit rim and a moving caustic shimmer. Terrain is shaded per pixel from the mesh plus the SDF (sediment bands in dirt, obsidian facets in rock, rim light that turns cyan near water). The scene then goes through a bloom pass. The UI is drawn after bloom so text stays crisp. Everything renders at the display's physical resolution.

**Levels proven by replay.** Each level is a small text file of shapes (rect, circle, polygon) painted onto the grid. Every level has a recorded solution in `tests/solutions/`: timed brush strokes, exactly what the mouse would produce. A test replays each one through the real `World` and checks it wins with the expected number of stars, and another checks that no level leaks water when left untouched. Because player edits are applied at the start of a fixed step and the simulation is deterministic, a replay is bit-identical to playing those strokes by hand.

## Benchmark

Measured on an Apple M5 (4 performance + 6 efficiency cores), Clang 21, Release build. Scene: a dam break in a box. A step is one 1/60 s frame of simulation (4 substeps). Each row is 600 steps x 5 repetitions; times are the mean / p95 per step.

| Particles | Brute force, 1 thread | Grid, 1 thread | Grid, 10 threads | 10 threads vs 1 |
|---:|---:|---:|---:|---:|
| 1,024 | 4.18 / 4.32 ms | 1.19 / 1.32 ms | 0.91 / 1.28 ms | 1.32x |
| 2,500 | 24.4 / 26.8 ms | 4.05 / 5.13 ms | 2.02 / 2.44 ms | 2.01x |
| 5,041 | skipped | 8.51 / 9.93 ms | 3.49 / 4.37 ms | 2.43x |
| 10,000 | skipped | 17.9 / 21.5 ms | 6.50 / 8.07 ms | 2.75x |
| 19,881 | skipped | 39.7 / 52.2 ms | 14.0 / 20.6 ms | 2.82x |

Largest particle count whose p95 step time stays within a 60 FPS frame (16.67 ms): **7,921 on 1 thread** and **20,449 on 10 threads**.

Some honest notes on these numbers:

- The grid is 3.5x faster than brute force at 1,024 particles and 6x faster at 2,500. Brute force was skipped from 5,000 up, because a single probe step there took 129 ms (5,041 particles) and a full 3,000-step run would have taken over the 2-minute limit.
- Threading reaches 2.8x on 10 cores, not 10x. The efficiency cores are much slower than the performance cores, the grid build is still serial, and the neighbour loops are limited by memory traffic. At 1,024 particles there isn't enough work to cover the cost of waking the threads.
- The p95 times are well above the means at large counts. That comes from the scene, not from jitter: in the first second of a dam break the water is compressed and every particle has more neighbours.
- Data layout: copying the neighbour data into cell order made the 1-thread step about 6% faster at ~20,000 particles (median of 5 paired runs) and made no measurable difference at ~5,000. The run-to-run noise was about the same size as the gain, so "about 6%" is approximate. Details in [`bench/results/data_layout.md`](bench/results/data_layout.md).

Full output with p95 times, machine details and the 60 FPS limit: [`bench/results/results.md`](bench/results/results.md) and [`results.csv`](bench/results/results.csv). The single-threaded profile is in [`bench/results/baseline.md`](bench/results/baseline.md).

## Decisions and tradeoffs

- **Gather-only passes.** Each pass reads the previous one and writes only the particle's own slot. That costs some memory (a separate output velocity buffer, the cell-ordered copies) and computes each pair twice instead of once. In exchange there are no write conflicts, so threading needs no locks and the results don't depend on thread count. That determinism is what makes the replay tests and bug reproduction possible.
- **The visuals use the collision grid.** The terrain mesh comes from the same cells the particles collide with, and the blur is clamped back to the real occupancy. A smoother, prettier outline that didn't match would show water floating above or sinking into the ground. Drawing what the physics sees is more honest, and a 7.5 cm cell is small enough to look smooth.
- **Levels are proven with recorded solutions.** "This level is solvable" and "3 stars is possible" are claims a test can check, so they are tested. When I tuned the physics, these tests showed right away which levels moved.
- **Settle-then-freeze scoring.** Reaching the goal doesn't end the level: the water keeps running until it settles (nothing drained for 2.5 s and the rest nearly still), or the player presses Finish. The score is computed once at that moment and the simulation freezes, so the result card can never change after it appears. A loss is only declared after settling too, so a slow trickle can't be called a failure early.
- **A fixed timestep with substeps.** The physics always advances in 1/240 s substeps regardless of frame rate, so a level plays the same on a 60 Hz and a 144 Hz display. The cost is a cap on catch-up steps per frame, so a very slow machine runs the water in slow motion rather than unstably.

## What I'd do next

- **Rigid bodies with two-way coupling**, for example a floating crate that the water can push and that can dam a channel. The SDF collision would need moving obstacles.
- **Compute the force pass with symmetric pairs.** Each pair is evaluated twice today. Doing it once needs scattered writes, which means per-thread buffers or colouring the grid to stay deterministic.
- **Move the solver to the GPU.** The gather-only structure maps well to compute shaders, and the counting-sort grid has a standard parallel prefix-sum version.
- **A level editor in the game.** The text format is easy to write by hand, but placing pits and tuning slopes by eye with live water would be much faster.

## Layout

```
engine/   the simulation library (no graphics): SPH, grid, terrain + SDF, levels, world, thread pool
game/     raylib front end: scenes, renderer, shaders, levels, fonts
tests/    Catch2 tests and the recorded level solutions
bench/    headless benchmark and its results
```

## License

MIT, see [LICENSE](LICENSE). Fonts: Outfit and JetBrains Mono, both under the SIL Open Font License (included in `game/assets/fonts/`).
