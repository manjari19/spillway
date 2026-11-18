#pragma once

// Turns the blocky terrain grid into smooth filled outlines for rendering.
//
// 1. Smooth field: for each material, every cell centre gets a value in
//    [0, 1] = blurred occupancy (5x5 binomial kernel 1-4-6-4-1). Hard cell
//    edges become ramps, so the 0.5 contour is rounded instead of
//    stair-stepped. The kernel is symmetric, so a straight edge still lands
//    exactly on the true cell boundary. Gaps narrower than ~2 cells close
//    up visually, which matches physics: water (0.1 m wide particles) cannot
//    pass a 1-cell (0.075 m) gap either.
// 2. Marching squares: the grid of cell centres is split into squares. For
//    each square, the corners at or above 0.5 are "inside". Walking the four
//    corners in order and inserting a point wherever an edge crosses 0.5
//    (found by linear interpolation) gives the filled part of that square as
//    a convex polygon, which is then fanned into triangles. The two ambiguous
//    "saddle" cases are resolved with the average of the four corners.
// 3. Runs of completely filled squares in a row are merged into one quad,
//    which cuts the triangle count for big solid areas by ~10x.
//
// The level is split into 16x16-square chunks. After an edit only the chunks
// whose field values changed are re-meshed.
//
// Collision still uses the exact cell grid; this mesh is purely visual.

#include <array>
#include <cstdint>
#include <vector>

#include "spillway/terrain.h"
#include "spillway/vec2.h"

namespace spillway {

class TerrainMesher {
public:
    static constexpr int kChunk = 16;
    static constexpr float kIso = 0.5f;
    // Materials that get a mesh, in draw order.
    static constexpr std::array<Cell, 3> kMaterials{Cell::Dirt, Cell::Rock, Cell::Wall};

    void build(const Terrain& terrain);
    // Re-meshes chunks touched by edits since the last call. Returns how many
    // chunks were rebuilt.
    int update(const Terrain& terrain);
    // Same, for an explicit dirty cell rectangle (caller took it from the terrain).
    int updateRect(const Terrain& terrain, CellRect dirty);
    // Cells whose field() value can change when the cells in `dirty` change.
    static CellRect fieldFootprint(const Terrain& terrain, CellRect dirty);

    int chunksX() const { return chunksX_; }
    int chunksY() const { return chunksY_; }
    int chunkCount() const { return chunksX_ * chunksY_; }
    // Triangle list (3 vertices per triangle) for one chunk and material.
    const std::vector<Vec2>& triangles(int chunk, int materialIndex) const {
        return meshes_[static_cast<size_t>(chunk) * kMaterials.size() + static_cast<size_t>(materialIndex)];
    }
    // Increments whenever any chunk changes.
    uint64_t version() const { return version_; }

    // Total filled area of one material, in m^2 (used by tests).
    float area(int materialIndex) const;

    // Smoothed occupancy of `material` at cell centre (x, y), in [0, 1].
    // Coordinates outside the grid are clamped to the nearest edge cell.
    static float field(const Terrain& terrain, Cell material, int x, int y);
    // Smoothed occupancy of any solid material (sum over materials).
    static float solidField(const Terrain& terrain, int x, int y);
    static constexpr int kBlurRadius = 2;

    // Marching squares over squares [sx0, sx1] x [sy0, sy1]. Square (sx, sy)
    // spans cell centres sx..sx+1 and sy..sy+1; sx ranges from -1 to width-1
    // so the outline reaches the level border.
    static void meshRegion(const Terrain& terrain, Cell material, int sx0, int sy0, int sx1, int sy1,
                           std::vector<Vec2>& out);

private:
    void meshChunk(const Terrain& terrain, int cx, int cy);

    int chunksX_ = 0;
    int chunksY_ = 0;
    std::vector<std::vector<Vec2>> meshes_;
    uint64_t version_ = 0;
};

} // namespace spillway
