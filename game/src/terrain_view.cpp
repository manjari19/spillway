#include "terrain_view.h"

#include <rlgl.h>

#include "assets.h"
#include "renderer.h"
#include "theme.h"

using namespace spillway;

namespace {
void setVec3(Shader s, int loc, Color c) {
    const float v[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
    SetShaderValue(s, loc, v, SHADER_UNIFORM_VEC3);
}
} // namespace

TerrainView::~TerrainView() {
    if (target_.id) UnloadRenderTexture(target_);
    if (sdfTex_.id) UnloadTexture(sdfTex_);
}

void TerrainView::reset(const Terrain& terrain) {
    mesher_.build(terrain);
    terrain.takeMeshDirty();
    drawnMeshVersion_ = ~0ull;
    sdfVersion_ = ~0ull;
    if (sdfTex_.id && (sdfW_ != terrain.width() + 1 || sdfH_ != terrain.height() + 1)) {
        UnloadTexture(sdfTex_);
        sdfTex_ = {};
    }
    if (!sdfTex_.id) {
        sdfW_ = terrain.width() + 1;
        sdfH_ = terrain.height() + 1;
        Image img{const_cast<float*>(terrain.sdfNodes().data()), sdfW_, sdfH_, 1, PIXELFORMAT_UNCOMPRESSED_R32};
        sdfTex_ = LoadTextureFromImage(img);
        SetTextureFilter(sdfTex_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(sdfTex_, TEXTURE_WRAP_CLAMP);
    }
}

int TerrainView::sync(const Terrain& terrain) {
    const int chunks = mesher_.update(terrain);
    if (terrain.version() != sdfVersion_) {
        sdfVersion_ = terrain.version();
        UpdateTexture(sdfTex_, terrain.sdfNodes().data());
    }
    return chunks;
}

void TerrainView::redrawMesh(const Renderer& renderer, const View& view) {
    const int rw = renderer.renderWidth(), rh = renderer.renderHeight();
    if (!target_.id || target_.texture.width != rw || target_.texture.height != rh) {
        if (target_.id) UnloadRenderTexture(target_);
        target_ = LoadRenderTexture(rw, rh);
        SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
    }
    BeginTextureMode(target_);
    ClearBackground(BLANK);
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlScalef(renderer.dpi(), renderer.dpi(), 1.0f);
    // Material id goes in the colour channel: R = dirt, G = rock, B = wall.
    const Color ids[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    for (int ch = 0; ch < mesher_.chunkCount(); ++ch) {
        for (int m = 0; m < 3; ++m) {
            const auto& tris = mesher_.triangles(ch, m);
            if (tris.empty()) continue;
            rlBegin(RL_TRIANGLES);
            rlColor4ub(ids[m].r, ids[m].g, ids[m].b, 255);
            for (const Vec2 p : tris) {
                const Vector2 s = view.toScreen(p);
                rlVertex2f(s.x, s.y);
            }
            rlEnd();
        }
    }
    rlPopMatrix();
    rlEnableBackfaceCulling();
    EndTextureMode();
    drawnMeshVersion_ = mesher_.version();
    drawnView_ = view;
}

void TerrainView::prepare(const Renderer& renderer, const View& view) {
    if (drawnMeshVersion_ != mesher_.version() || !(drawnView_ == view) || !target_.id ||
        target_.texture.width != renderer.renderWidth() || target_.texture.height != renderer.renderHeight())
        redrawMesh(renderer, view);
}

void TerrainView::draw(const Renderer& renderer, const View& view, const Terrain& terrain) {
    TerrainShader& ts = assets().terrain;
    const float sdfSize[2] = {static_cast<float>(sdfW_), static_cast<float>(sdfH_)};
    const float cell = terrain.cellSize();
    const float texel[2] = {1.0f / static_cast<float>(renderer.renderWidth()), 1.0f / static_cast<float>(renderer.renderHeight())};
    const float rh = static_cast<float>(renderer.renderHeight()), dpi = renderer.dpi();
    const float offset[2] = {view.offset.x, view.offset.y};
    SetShaderValue(ts.shader, ts.sdfSize, sdfSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(ts.shader, ts.cellSize, &cell, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ts.shader, ts.texel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(ts.shader, ts.renderHeight, &rh, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ts.shader, ts.dpi, &dpi, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ts.shader, ts.viewOffset, offset, SHADER_UNIFORM_VEC2);
    SetShaderValue(ts.shader, ts.viewScale, &view.scale, SHADER_UNIFORM_FLOAT);
    setVec3(ts.shader, ts.dirtColor, theme::dirt);
    setVec3(ts.shader, ts.dirtTopColor, theme::dirtTop);
    setVec3(ts.shader, ts.rockColor, theme::rock);
    setVec3(ts.shader, ts.rockEdgeColor, theme::rockEdge);
    setVec3(ts.shader, ts.wallColor, theme::wall);

    BeginShaderMode(ts.shader);
    SetShaderValueTexture(ts.shader, ts.sdf, sdfTex_);
    const Texture2D& t = target_.texture;
    DrawTexturePro(t, {0, 0, static_cast<float>(t.width), -static_cast<float>(t.height)},
                   {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, {0, 0}, 0.0f, WHITE);
    EndShaderMode();
}
