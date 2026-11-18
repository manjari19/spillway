#pragma once

// Draws the terrain: marching-squares meshes rendered into a cached target
// (only when the mesh or view changes), then shaded per pixel with the SDF.

#include <raylib.h>

#include <vector>

#include "spillway/marching_squares.h"
#include "spillway/terrain.h"
#include "view.h"

class Renderer;

class TerrainView {
public:
    ~TerrainView();
    void reset(const spillway::Terrain& terrain);
    // Re-mesh edited chunks and refresh the SDF texture. Returns chunks re-meshed.
    int sync(const spillway::Terrain& terrain);
    // Re-render the cached mesh target if the mesh or view changed. Call
    // before Renderer::beginWorld (it draws into its own target).
    void prepare(const Renderer& renderer, const View& view);
    // Shade the terrain into the current world layer.
    void draw(const Renderer& renderer, const View& view, const spillway::Terrain& terrain);
    // Record dug cells so their new edges glow briefly.
    void markDug(const spillway::Terrain& terrain, const std::vector<spillway::Vec2>& cells, float now);

private:
    void redrawMesh(const Renderer& renderer, const View& view);
    void updateField(const spillway::Terrain& terrain, spillway::CellRect region);

    spillway::TerrainMesher mesher_;
    RenderTexture2D target_{};
    Texture2D sdfTex_{};
    Texture2D fieldTex_{};          // smoothed solid occupancy, one texel per cell
    std::vector<float> field_;
    Texture2D dugTex_{};            // per cell: time it was last dug near (fresh-cut glow)
    std::vector<float> dug_;
    int sdfW_ = 0, sdfH_ = 0;
    uint64_t sdfVersion_ = ~0ull;
    uint64_t drawnMeshVersion_ = ~0ull;
    View drawnView_{};
};
