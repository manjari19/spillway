#pragma once

// Uniform-grid spatial hash for neighbour search.
//
// The world is split into square cells whose side equals the kernel radius h.
// Any particle within distance h of point p must then lie in p's cell or one
// of the 8 surrounding cells, so a neighbour query touches 3x3 cells instead
// of every particle: O(n * k) instead of O(n^2).
//
// Because levels have fixed bounds, the "hash" is simply the linear cell
// index y * cellsX + x, so there are no hash collisions. The grid is rebuilt
// every substep with a counting sort:
//   1. count particles per cell
//   2. exclusive prefix sum -> cellStart (where each cell's run begins)
//   3. scatter particle indices into sortedIndex
// Scattering in particle order means indices inside a cell are ascending, so
// iteration order (and therefore floating-point summation order) is fully
// deterministic.

#include <cstdint>
#include <span>
#include <vector>

#include "spillway/vec2.h"

namespace spillway {

class SpatialGrid {
public:
    void configure(Vec2 worldMin, Vec2 worldMax, float cellSize);
    void build(std::span<const Vec2> positions);

    // Calls fn(j) for every particle j in the 3x3 cell block around p.
    // These are *candidates*; the caller still checks the real distance.
    template <class Fn>
    void forEachCandidate(Vec2 p, Fn&& fn) const {
        const int cx = cellX(p.x);
        const int cy = cellY(p.y);
        const int x0 = cx > 0 ? cx - 1 : 0;
        const int x1 = cx < cellsX_ - 1 ? cx + 1 : cellsX_ - 1;
        const int y0 = cy > 0 ? cy - 1 : 0;
        const int y1 = cy < cellsY_ - 1 ? cy + 1 : cellsY_ - 1;
        for (int y = y0; y <= y1; ++y) {
            // Cells x0..x1 in one row are consecutive in the sorted array,
            // so the whole row is a single contiguous range.
            const uint32_t begin = cellStart_[static_cast<size_t>(y * cellsX_ + x0)];
            const uint32_t end = cellStart_[static_cast<size_t>(y * cellsX_ + x1 + 1)];
            for (uint32_t k = begin; k < end; ++k) fn(sortedIndex_[k]);
        }
    }

    // Same 3x3 block, as ranges [begin, end) of positions in the sorted
    // order (one range per row). Lets callers read per-particle data that has
    // been copied into cell order, which is contiguous in memory.
    template <class Fn>
    void forEachCandidateRange(Vec2 p, Fn&& fn) const {
        const int cx = cellX(p.x);
        const int cy = cellY(p.y);
        const int x0 = cx > 0 ? cx - 1 : 0;
        const int x1 = cx < cellsX_ - 1 ? cx + 1 : cellsX_ - 1;
        const int y0 = cy > 0 ? cy - 1 : 0;
        const int y1 = cy < cellsY_ - 1 ? cy + 1 : cellsY_ - 1;
        for (int y = y0; y <= y1; ++y)
            fn(cellStart_[static_cast<size_t>(y * cellsX_ + x0)], cellStart_[static_cast<size_t>(y * cellsX_ + x1 + 1)]);
    }
    // Particle index at each sorted position.
    const std::vector<uint32_t>& sortedIndex() const { return sortedIndex_; }

    int cellsX() const { return cellsX_; }
    int cellsY() const { return cellsY_; }
    float cellSize() const { return cellSize_; }
    Vec2 origin() const { return origin_; }
    uint32_t cellCount(int x, int y) const {
        const size_t c = static_cast<size_t>(y * cellsX_ + x);
        return cellStart_[c + 1] - cellStart_[c];
    }

    // Clamp to the grid so particles slightly outside the bounds still land
    // in an edge cell. Clamping never moves two points further apart in cell
    // space, so the 3x3 query stays correct.
    int cellX(float x) const { return clampCell((x - origin_.x) * invCellSize_, cellsX_); }
    int cellY(float y) const { return clampCell((y - origin_.y) * invCellSize_, cellsY_); }

private:
    static int clampCell(float f, int n) {
        if (!(f >= 0.0f)) return 0; // also catches NaN
        if (f >= static_cast<float>(n)) return n - 1;
        return static_cast<int>(f);
    }

    Vec2 origin_{};
    float cellSize_ = 1.0f;
    float invCellSize_ = 1.0f;
    int cellsX_ = 1;
    int cellsY_ = 1;
    std::vector<uint32_t> cellStart_;    // size cells + 1
    std::vector<uint32_t> sortedIndex_;  // particle indices sorted by cell
    std::vector<uint32_t> particleCell_; // cell of each particle
    std::vector<uint32_t> cursor_;       // scratch for the scatter pass
};

} // namespace spillway
