# Data layout experiment: cell-ordered neighbour data

**Change:** after the grid is rebuilt, the values the neighbour loops read
(predicted position, then density, near-density and velocity) are copied into
cell order. Each row of the 3x3 neighbour block becomes one contiguous read,
instead of a gather through the grid's index array.

The neighbour order is the same as before, so every floating-point sum is the
same. The level replays finish on exactly the same step with the same scores.

**Method:**
- Two builds of the same code (Clang, -O3), one with the copies and one
  reading through the index ("old").
- Each run is `spillway_bench --baseline`: dam-break scene, 1 thread, 600
  steps x 5 repetitions per count.
- Builds were alternated old/new five times on the same machine (Apple M5,
  4 performance + 6 efficiency cores). The machine warmed up over the session,
  so later pairs are slower for both builds.

Mean step time in ms (a step is one 1/60 s frame, 4 substeps):

| Pair | 5,041 old | 5,041 new | 19,881 old | 19,881 new |
|---:|---:|---:|---:|---:|
| 1 | 7.830 | 7.859 | 35.34 | 32.23 |
| 2 | 7.812 | 8.197 | 35.13 | 40.43 |
| 3 | 8.420 | 8.369 | 41.16 | 38.06 |
| 4 | 9.228 | 8.350 | 41.99 | 38.59 |
| 5 | 9.435 | 9.608 | 45.50 | 40.02 |
| **Median** | **8.420** | **8.369** | **41.16** | **38.59** |

**Result:**
- **About 20,000 particles:** the new layout was faster in 4 of 5 pairs, and the
  median is about 6% lower.
- **About 5,000 particles:** no measurable difference. The data mostly fits in
  cache there anyway.

Kept, because it helps at the larger size and costs nothing in correctness.
The noise between runs (pair 2) is about the same size as the gain, so treat
"about 6%" as approximate.

The earlier single run of each build (`baseline.md` was produced before the
change, then once after) showed 38.45 -> 32.56 ms at 20k. The paired runs
above are the more careful comparison.
