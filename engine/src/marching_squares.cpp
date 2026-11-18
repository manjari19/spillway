#include "spillway/marching_squares.h"

#include <algorithm>
#include <cmath>

namespace spillway {

float TerrainMesher::field(const Terrain& t, Cell material, int x, int y) {
    // 5x5 binomial blur of the 0/1 occupancy: separable weights 1 4 6 4 1.
    static constexpr int k[5] = {1, 4, 6, 4, 1};
    int sum = 0;
    for (int j = -kBlurRadius; j <= kBlurRadius; ++j) {
        const int yy = std::clamp(y + j, 0, t.height() - 1);
        for (int i = -kBlurRadius; i <= kBlurRadius; ++i) {
            const int xx = std::clamp(x + i, 0, t.width() - 1);
            if (t.at(xx, yy) == material) sum += k[i + kBlurRadius] * k[j + kBlurRadius];
        }
    }
    const float v = static_cast<float>(sum) / 256.0f;
    // Keep every cell centre on its true side of the contour, so even a
    // single-cell crumb is drawn (visuals must never hide something water
    // collides with). Edge positions between centres still come from the
    // smooth values, which is what removes the stair-steps.
    const Cell centre = t.at(std::clamp(x, 0, t.width() - 1), std::clamp(y, 0, t.height() - 1));
    return centre == material ? std::max(v, 0.51f) : std::min(v, 0.49f);
}

float TerrainMesher::solidField(const Terrain& t, int x, int y) {
    // Plain blurred occupancy of any solid (no clamping): used for shading.
    static constexpr int k[5] = {1, 4, 6, 4, 1};
    int sum = 0;
    for (int j = -kBlurRadius; j <= kBlurRadius; ++j) {
        const int yy = std::clamp(y + j, 0, t.height() - 1);
        for (int i = -kBlurRadius; i <= kBlurRadius; ++i) {
            const int xx = std::clamp(x + i, 0, t.width() - 1);
            if (isSolid(t.at(xx, yy))) sum += k[i + kBlurRadius] * k[j + kBlurRadius];
        }
    }
    return static_cast<float>(sum) / 256.0f;
}

CellRect TerrainMesher::fieldFootprint(const Terrain& t, CellRect d) {
    if (d.empty()) return d;
    return {std::max(0, d.x0 - kBlurRadius), std::max(0, d.y0 - kBlurRadius), std::min(t.width() - 1, d.x1 + kBlurRadius),
            std::min(t.height() - 1, d.y1 + kBlurRadius)};
}

void TerrainMesher::meshRegion(const Terrain& t, Cell material, int sx0, int sy0, int sx1, int sy1,
                               std::vector<Vec2>& out) {
    out.clear();
    const float c = t.cellSize();
    const Vec2 ext = t.extent();
    // Sample values for this region, including the +1 corner row/column.
    const int nx = sx1 - sx0 + 2, ny = sy1 - sy0 + 2;
    std::vector<float> v(static_cast<size_t>(nx * ny));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) v[static_cast<size_t>(j * nx + i)] = field(t, material, sx0 + i, sy0 + j);
    auto val = [&](int sx, int sy) { return v[static_cast<size_t>((sy - sy0) * nx + (sx - sx0))]; };

    // Cell centre position, clamped to the level so the half-cell border
    // strip is covered without spilling outside it.
    auto pos = [&](float sx, float sy) {
        return Vec2{std::clamp((sx + 0.5f) * c, 0.0f, ext.x), std::clamp((sy + 0.5f) * c, 0.0f, ext.y)};
    };
    auto tri = [&](Vec2 a, Vec2 b, Vec2 d) { out.push_back(a); out.push_back(b); out.push_back(d); };

    for (int sy = sy0; sy <= sy1; ++sy) {
        // Start of a run of fully-inside squares. Square indices start at -1,
        // so "no run" needs a sentinel outside that range.
        constexpr int kNoRun = -1000000;
        int runStart = kNoRun;
        auto flushRun = [&](int endExclusive) {
            if (runStart == kNoRun) return;
            const Vec2 a = pos(static_cast<float>(runStart), static_cast<float>(sy));
            const Vec2 b = pos(static_cast<float>(endExclusive), static_cast<float>(sy + 1));
            tri(a, {b.x, a.y}, b);
            tri(a, b, {a.x, b.y});
            runStart = kNoRun;
        };

        for (int sx = sx0; sx <= sx1; ++sx) {
            // Corners in walk order: top-left, top-right, bottom-right, bottom-left.
            const float cv[4] = {val(sx, sy), val(sx + 1, sy), val(sx + 1, sy + 1), val(sx, sy + 1)};
            const Vec2 cp[4] = {pos(static_cast<float>(sx), static_cast<float>(sy)),
                                pos(static_cast<float>(sx + 1), static_cast<float>(sy)),
                                pos(static_cast<float>(sx + 1), static_cast<float>(sy + 1)),
                                pos(static_cast<float>(sx), static_cast<float>(sy + 1))};
            int mask = 0;
            for (int k = 0; k < 4; ++k) mask |= (cv[k] >= kIso ? 1 : 0) << k;

            if (mask == 15) {
                if (runStart == kNoRun) runStart = sx;
                continue;
            }
            flushRun(sx);
            if (mask == 0) continue;

            // Point where edge a->b crosses the iso value.
            auto cross = [&](int a, int b) {
                const float t01 = (kIso - cv[a]) / (cv[b] - cv[a]);
                return cp[a] + (cp[b] - cp[a]) * t01;
            };

            // Saddles: opposite corners inside. If the centre is outside the
            // two inside corners are separate, so emit two corner triangles.
            const bool saddle = mask == 5 || mask == 10;
            const float centre = 0.25f * (cv[0] + cv[1] + cv[2] + cv[3]);
            if (saddle && centre < kIso) {
                for (int k = 0; k < 4; ++k) {
                    if (!(mask & (1 << k))) continue;
                    const int prev = (k + 3) % 4, next = (k + 1) % 4;
                    tri(cp[k], cross(k, next), cross(k, prev));
                }
                continue;
            }

            // General case: walk the square boundary collecting inside
            // corners and crossing points -> convex polygon -> triangle fan.
            Vec2 poly[8];
            int n = 0;
            for (int k = 0; k < 4; ++k) {
                const int next = (k + 1) % 4;
                const bool in = mask & (1 << k), inNext = mask & (1 << next);
                if (in) poly[n++] = cp[k];
                if (in != inNext) poly[n++] = cross(k, next);
            }
            for (int k = 1; k + 1 < n; ++k) tri(poly[0], poly[k], poly[k + 1]);
        }
        flushRun(sx1 + 1);
    }
}

void TerrainMesher::build(const Terrain& t) {
    // Squares run from -1 to width-1 (width+1 of them), same for height.
    chunksX_ = (t.width() + 1 + kChunk - 1) / kChunk;
    chunksY_ = (t.height() + 1 + kChunk - 1) / kChunk;
    meshes_.assign(static_cast<size_t>(chunksX_ * chunksY_) * kMaterials.size(), {});
    for (int cy = 0; cy < chunksY_; ++cy)
        for (int cx = 0; cx < chunksX_; ++cx) meshChunk(t, cx, cy);
    ++version_;
}

void TerrainMesher::meshChunk(const Terrain& t, int cx, int cy) {
    const int sx0 = cx * kChunk - 1, sy0 = cy * kChunk - 1;
    const int sx1 = std::min(sx0 + kChunk - 1, t.width() - 1);
    const int sy1 = std::min(sy0 + kChunk - 1, t.height() - 1);
    for (size_t m = 0; m < kMaterials.size(); ++m) {
        auto& mesh = meshes_[static_cast<size_t>(cy * chunksX_ + cx) * kMaterials.size() + m];
        meshRegion(t, kMaterials[m], sx0, sy0, sx1, sy1, mesh);
    }
}

int TerrainMesher::update(const Terrain& t) { return updateRect(t, t.takeMeshDirty()); }

int TerrainMesher::updateRect(const Terrain& t, CellRect d) {
    if (d.empty()) return 0;
    // A cell change alters field values kBlurRadius cells around it, and a
    // sample is a corner of the squares to its left/top. So squares
    // [x0 - R - 1, x1 + R] are affected; square sx lives in chunk (sx + 1) / kChunk.
    const int R = kBlurRadius;
    const int cx0 = std::clamp((d.x0 - R - 1 + 1) / kChunk, 0, chunksX_ - 1);
    const int cx1 = std::clamp((d.x1 + R + 1) / kChunk, 0, chunksX_ - 1);
    const int cy0 = std::clamp((d.y0 - R - 1 + 1) / kChunk, 0, chunksY_ - 1);
    const int cy1 = std::clamp((d.y1 + R + 1) / kChunk, 0, chunksY_ - 1);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) meshChunk(t, cx, cy);
    ++version_;
    return (cx1 - cx0 + 1) * (cy1 - cy0 + 1);
}

float TerrainMesher::area(int materialIndex) const {
    double a = 0.0;
    for (int ch = 0; ch < chunkCount(); ++ch) {
        const auto& tris = triangles(ch, materialIndex);
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec2 e1 = tris[i + 1] - tris[i], e2 = tris[i + 2] - tris[i];
            a += 0.5 * std::abs(static_cast<double>(e1.x) * e2.y - static_cast<double>(e1.y) * e2.x);
        }
    }
    return static_cast<float>(a);
}

} // namespace spillway
