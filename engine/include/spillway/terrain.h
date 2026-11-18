#pragma once

// Level geometry: a uniform grid of cells plus a signed distance field (SDF).
//
// Cell size: levels use 0.075 m, i.e. 1.5x the particle collision radius
// (0.05 m). Reasons:
//   - smaller than the rest spacing of particles (0.135 m), so dug channels
//     and drawn walls look smooth relative to the water;
//   - it is the anti-tunneling step limit: a particle may move at most one
//     cell per substep (0.075 m * 240 substeps/s = 18 m/s top speed), and
//     the swept DDA test then only ever crosses 2-3 cells;
//   - the SDF is sampled at cell corners, so 0.075 m is fine enough to
//     resolve a 0.05 m particle radius without being expensive
//     (16 x 9 m level = 214 x 120 = 25.7k cells).
//
// SDF: phi(p) = distance to the nearest solid boundary, negative inside solid.
// It is stored at cell corners ("nodes") and bilinearly interpolated, which
// gives a continuous field with a well-defined gradient (the surface normal).
// Each node value is computed exactly as the distance from the node to the
// nearest solid (or, inside, empty) cell square, searched within a narrow band
// of kSdfBand cells. Collision only needs phi near surfaces, so values
// further away are clamped to +/- band.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "spillway/vec2.h"

namespace spillway {

enum class Cell : uint8_t { Empty = 0, Dirt, Rock, Wall };

constexpr bool isSolid(Cell c) { return c != Cell::Empty; }

// Inclusive rectangle of cell (or node) coordinates.
struct CellRect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool empty() const { return x1 < x0 || y1 < y0; }
    int area() const { return empty() ? 0 : (x1 - x0 + 1) * (y1 - y0 + 1); }
};

class Terrain {
public:
    // SDF band half-width in cells. An edited cell changes node values up to
    // this many cells away, so this also sets the size of the dirty region.
    static constexpr int kSdfBand = 4;

    Terrain(int cellsX, int cellsY, float cellSize);

    int width() const { return w_; }
    int height() const { return h_; }
    float cellSize() const { return cell_; }
    Vec2 extent() const { return {static_cast<float>(w_) * cell_, static_cast<float>(h_) * cell_}; }

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    // Outside the grid counts as rock, so water can never leave the level.
    Cell at(int x, int y) const { return inBounds(x, y) ? cells_[index(x, y)] : Cell::Rock; }
    Cell atPoint(Vec2 p) const { return at(cellX(p.x), cellY(p.y)); }
    void set(int x, int y, Cell c);
    void fill(Cell c);
    int count(Cell c) const;

    int cellX(float x) const { return static_cast<int>(std::floor(x / cell_)); }
    int cellY(float y) const { return static_cast<int>(std::floor(y / cell_)); }
    Vec2 cellCentre(int x, int y) const {
        return {(static_cast<float>(x) + 0.5f) * cell_, (static_cast<float>(y) + 0.5f) * cell_};
    }

    // Visits every in-bounds cell whose centre lies inside the disc, in
    // row-major order (deterministic).
    template <class Fn>
    void forEachCellInDisc(Vec2 centre, float radius, Fn&& fn) const {
        const int x0 = std::max(0, cellX(centre.x - radius));
        const int x1 = std::min(w_ - 1, cellX(centre.x + radius));
        const int y0 = std::max(0, cellY(centre.y - radius));
        const int y1 = std::min(h_ - 1, cellY(centre.y + radius));
        const float r2 = radius * radius;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (lengthSq(cellCentre(x, y) - centre) <= r2) fn(x, y);
    }

    // Brush edits; each returns the number of cells changed.
    int dig(Vec2 centre, float radius);   // Dirt -> Empty
    int erase(Vec2 centre, float radius); // Wall -> Empty

    // --- signed distance field ---
    float distance(Vec2 p) const;  // bilinear interpolation of node values
    Vec2 gradient(Vec2 p) const;   // d(phi)/dp, points away from solid
    float nodeDistance(int i, int j) const { return sdf_[nodeIndex(i, j)]; }
    void rebuildSdf();             // whole level (level load)
    // Recompute only nodes affected by cells edited since the last call.
    // Returns the number of nodes recomputed.
    int updateSdf();
    CellRect lastSdfRegion() const { return lastSdfRegion_; }

    // Swept test for anti-tunneling. Walks every cell the segment a->b passes
    // through (Amanatides & Woo grid traversal). If one is solid, returns true
    // and the segment parameter t in [0, 1] where it enters that cell.
    bool segmentHitsSolid(Vec2 a, Vec2 b, float& tHit) const;

    // Increments on every change; renderers use it to know when to redraw.
    uint64_t version() const { return version_; }

private:
    size_t index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x); }
    size_t nodeIndex(int i, int j) const { return static_cast<size_t>(j) * static_cast<size_t>(w_ + 1) + static_cast<size_t>(i); }
    float computeNode(int i, int j) const;
    void markDirty(int x, int y);

    int w_, h_;
    float cell_;
    std::vector<Cell> cells_;
    std::vector<float> sdf_; // (w+1) x (h+1) node values, metres
    CellRect dirty_{};       // cells edited since last updateSdf()
    CellRect lastSdfRegion_{};
    uint64_t version_ = 0;
};

} // namespace spillway
