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
    if (fieldTex_.id) UnloadTexture(fieldTex_);
    if (dugTex_.id) UnloadTexture(dugTex_);
}

void TerrainView::markDug(const Terrain& t, const std::vector<Vec2>& cells, float now) {
    if (cells.empty()) return;
    const int w = t.width(), h = t.height();
    int y0 = h, y1 = -1;
    // Stamp the dug cell and its neighbours, so the solid cells that became
    // the new edge are covered too.
    for (const Vec2 p : cells) {
        const int cx = t.cellX(p.x), cy = t.cellY(p.y);
        for (int y = std::max(0, cy - 2); y <= std::min(h - 1, cy + 2); ++y)
            for (int x = std::max(0, cx - 2); x <= std::min(w - 1, cx + 2); ++x) dug_[static_cast<size_t>(y * w + x)] = now;
        y0 = std::min(y0, std::max(0, cy - 2));
        y1 = std::max(y1, std::min(h - 1, cy + 2));
    }
    const Rectangle rows{0, static_cast<float>(y0), static_cast<float>(w), static_cast<float>(y1 - y0 + 1)};
    UpdateTextureRec(dugTex_, rows, dug_.data() + static_cast<size_t>(y0 * w));
}

void TerrainView::updateField(const Terrain& t, CellRect r) {
    if (r.empty()) return;
    const int w = t.width();
    for (int y = r.y0; y <= r.y1; ++y)
        for (int x = r.x0; x <= r.x1; ++x)
            field_[static_cast<size_t>(y * w + x)] = TerrainMesher::solidField(t, x, y);
    // Upload just the changed rows (a row range is contiguous in memory).
    const Rectangle rows{0, static_cast<float>(r.y0), static_cast<float>(w), static_cast<float>(r.y1 - r.y0 + 1)};
    UpdateTextureRec(fieldTex_, rows, field_.data() + static_cast<size_t>(r.y0 * w));
}

void TerrainView::reset(const Terrain& terrain) {
    mesher_.build(terrain);
    terrain.takeMeshDirty();
    if (fieldTex_.id) UnloadTexture(fieldTex_);
    field_.assign(static_cast<size_t>(terrain.width() * terrain.height()), 0.0f);
    Image fimg{field_.data(), terrain.width(), terrain.height(), 1, PIXELFORMAT_UNCOMPRESSED_R32};
    fieldTex_ = LoadTextureFromImage(fimg);
    SetTextureFilter(fieldTex_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(fieldTex_, TEXTURE_WRAP_CLAMP);
    updateField(terrain, {0, 0, terrain.width() - 1, terrain.height() - 1});
    if (dugTex_.id) UnloadTexture(dugTex_);
    dug_.assign(field_.size(), -1000.0f);
    Image dimg{dug_.data(), terrain.width(), terrain.height(), 1, PIXELFORMAT_UNCOMPRESSED_R32};
    dugTex_ = LoadTextureFromImage(dimg);
    SetTextureFilter(dugTex_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(dugTex_, TEXTURE_WRAP_CLAMP);
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
    // Only the edited region is re-meshed and re-uploaded.
    const CellRect dirty = terrain.takeMeshDirty();
    const int chunks = mesher_.updateRect(terrain, dirty);
    updateField(terrain, TerrainMesher::fieldFootprint(terrain, dirty));
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
    const Vec2 ext = terrain.extent();
    const float levelSize[2] = {ext.x, ext.y};
    const float now = static_cast<float>(GetTime());
    SetShaderValue(ts.shader, ts.levelSize, levelSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(ts.shader, ts.time, &now, SHADER_UNIFORM_FLOAT);
    setVec3(ts.shader, ts.strataA, theme::strataA);
    setVec3(ts.shader, ts.strataB, theme::strataB);
    setVec3(ts.shader, ts.strataC, theme::strataC);
    setVec3(ts.shader, ts.rimColor, theme::rimLight);
    setVec3(ts.shader, ts.waterTint, theme::waterTint);
    setVec3(ts.shader, ts.rockBody, theme::rock);
    setVec3(ts.shader, ts.rockEdge, theme::rockEdge);
    setVec3(ts.shader, ts.rockEdgeAlt, theme::rockEdgeAlt);
    setVec3(ts.shader, ts.wallColor, theme::wall);

    const float fieldSize[2] = {static_cast<float>(terrain.width()), static_cast<float>(terrain.height())};
    SetShaderValue(ts.shader, ts.fieldSize, fieldSize, SHADER_UNIFORM_VEC2);
    BeginShaderMode(ts.shader);
    SetShaderValueTexture(ts.shader, ts.sdf, sdfTex_);
    SetShaderValueTexture(ts.shader, ts.field, fieldTex_);
    SetShaderValueTexture(ts.shader, ts.waterField, renderer.waterField());
    SetShaderValueTexture(ts.shader, ts.dugTime, dugTex_);
    const Texture2D& t = target_.texture;
    DrawTexturePro(t, {0, 0, static_cast<float>(t.width), -static_cast<float>(t.height)},
                   {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, {0, 0}, 0.0f, WHITE);
    EndShaderMode();
}
