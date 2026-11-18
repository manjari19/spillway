#include "spillway/spatial_grid.h"

#include <algorithm>
#include <cmath>

namespace spillway {

void SpatialGrid::configure(Vec2 worldMin, Vec2 worldMax, float cellSize) {
    origin_ = worldMin;
    cellSize_ = cellSize;
    invCellSize_ = 1.0f / cellSize;
    cellsX_ = std::max(1, static_cast<int>(std::ceil((worldMax.x - worldMin.x) * invCellSize_)));
    cellsY_ = std::max(1, static_cast<int>(std::ceil((worldMax.y - worldMin.y) * invCellSize_)));
    cellStart_.assign(static_cast<size_t>(cellsX_ * cellsY_) + 1, 0);
}

void SpatialGrid::build(std::span<const Vec2> positions) {
    const size_t n = positions.size();
    const size_t cells = static_cast<size_t>(cellsX_ * cellsY_);
    particleCell_.resize(n);
    sortedIndex_.resize(n);

    // 1. Count particles per cell. cellStart_[c + 1] holds the count of cell c
    //    so that the prefix sum below turns it into a start offset directly.
    std::fill(cellStart_.begin(), cellStart_.end(), 0u);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = static_cast<uint32_t>(cellY(positions[i].y) * cellsX_ + cellX(positions[i].x));
        particleCell_[i] = c;
        ++cellStart_[c + 1];
    }

    // 2. Prefix sum: cellStart_[c] = number of particles in cells before c.
    for (size_t c = 0; c < cells; ++c) cellStart_[c + 1] += cellStart_[c];

    // 3. Scatter. `cursor` tracks the next free slot of each cell. Visiting
    //    particles in index order keeps each cell's run sorted ascending.
    cursor_.assign(cellStart_.begin(), cellStart_.end() - 1);
    for (size_t i = 0; i < n; ++i) {
        sortedIndex_[cursor_[particleCell_[i]]++] = static_cast<uint32_t>(i);
    }
}

} // namespace spillway
