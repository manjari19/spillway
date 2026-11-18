#include "spillway/terrain.h"

#include <limits>

namespace spillway {

Terrain::Terrain(int cellsX, int cellsY, float cellSize)
    : w_(cellsX), h_(cellsY), cell_(cellSize),
      cells_(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsY), Cell::Empty),
      sdf_(static_cast<size_t>(cellsX + 1) * static_cast<size_t>(cellsY + 1), 0.0f) {
    rebuildSdf();
}

static void growRect(CellRect& r, int x, int y) {
    if (r.empty()) {
        r = {x, y, x, y};
    } else {
        r.x0 = std::min(r.x0, x);
        r.y0 = std::min(r.y0, y);
        r.x1 = std::max(r.x1, x);
        r.y1 = std::max(r.y1, y);
    }
}

void Terrain::markDirty(int x, int y) {
    growRect(dirty_, x, y);
    growRect(meshDirty_, x, y);
}

void Terrain::set(int x, int y, Cell c) {
    if (!inBounds(x, y)) return;
    Cell& cell = cells_[index(x, y)];
    if (cell == c) return;
    cell = c;
    markDirty(x, y);
    ++version_;
}

void Terrain::fill(Cell c) {
    std::fill(cells_.begin(), cells_.end(), c);
    dirty_ = {0, 0, w_ - 1, h_ - 1};
    meshDirty_ = dirty_;
    ++version_;
}

int Terrain::count(Cell c) const {
    return static_cast<int>(std::count(cells_.begin(), cells_.end(), c));
}

int Terrain::dig(Vec2 centre, float radius) {
    int changed = 0;
    forEachCellInDisc(centre, radius, [&](int x, int y) {
        if (at(x, y) == Cell::Dirt) { set(x, y, Cell::Empty); ++changed; }
    });
    return changed;
}

int Terrain::erase(Vec2 centre, float radius) {
    int changed = 0;
    forEachCellInDisc(centre, radius, [&](int x, int y) {
        if (at(x, y) == Cell::Wall) { set(x, y, Cell::Empty); ++changed; }
    });
    return changed;
}

// Exact distance from node (i, j) to the nearest solid cell square (if the
// node is outside) or nearest empty cell square (if inside), in the band.
//
// Node (i, j) sits at the top-left corner of cell (i, j). Cell x spans
// [x, x+1] in cell units, so the gap between node column i and cell x is
//   x - i        if the cell is to the right (x >= i)
//   i - (x + 1)  if it is to the left
// and 0 if the node touches the cell. Same for y. Distances are kept as
// integer squared cell counts until the end, which makes them exact.
float Terrain::computeNode(int i, int j) const {
    constexpr int B = kSdfBand;
    int out2 = B * B; // squared distance to nearest solid
    int in2 = B * B;  // squared distance to nearest empty
    for (int y = j - B; y < j + B; ++y) {
        const int dy = std::max({y - j, j - y - 1, 0});
        for (int x = i - B; x < i + B; ++x) {
            const int dx = std::max({x - i, i - x - 1, 0});
            const int d2 = dx * dx + dy * dy;
            if (isSolid(at(x, y))) out2 = std::min(out2, d2);
            else in2 = std::min(in2, d2);
        }
    }
    // A node touching solid has out2 == 0. If it also touches empty space it
    // is on the surface (phi = 0); if it is surrounded by solid it is inside
    // and phi = -(distance to nearest empty cell).
    const float d = out2 > 0 ? std::sqrt(static_cast<float>(out2)) : -std::sqrt(static_cast<float>(in2));
    return d * cell_;
}

void Terrain::rebuildSdf() {
    for (int j = 0; j <= h_; ++j)
        for (int i = 0; i <= w_; ++i) sdf_[nodeIndex(i, j)] = computeNode(i, j);
    dirty_ = {};
    lastSdfRegion_ = {0, 0, w_, h_};
}

int Terrain::updateSdf() {
    if (dirty_.empty()) {
        lastSdfRegion_ = {};
        return 0;
    }
    // Node i looks at cells [i - B, i + B - 1], so a change to cell x affects
    // nodes [x - B + 1, x + B]. The dirty region is the edited bounding box
    // grown by the band (4 cells = 0.3 m) on each side.
    constexpr int B = kSdfBand;
    CellRect r{std::max(0, dirty_.x0 - B + 1), std::max(0, dirty_.y0 - B + 1),
               std::min(w_, dirty_.x1 + B), std::min(h_, dirty_.y1 + B)};
    for (int j = r.y0; j <= r.y1; ++j)
        for (int i = r.x0; i <= r.x1; ++i) sdf_[nodeIndex(i, j)] = computeNode(i, j);
    dirty_ = {};
    lastSdfRegion_ = r;
    return r.area();
}

float Terrain::distance(Vec2 p) const {
    // Bilinear interpolation between the 4 nodes of the cell containing p.
    const float u = std::clamp(p.x / cell_, 0.0f, static_cast<float>(w_));
    const float v = std::clamp(p.y / cell_, 0.0f, static_cast<float>(h_));
    const int i = std::min(static_cast<int>(u), w_ - 1);
    const int j = std::min(static_cast<int>(v), h_ - 1);
    const float fx = u - static_cast<float>(i);
    const float fy = v - static_cast<float>(j);
    const float n00 = nodeDistance(i, j), n10 = nodeDistance(i + 1, j);
    const float n01 = nodeDistance(i, j + 1), n11 = nodeDistance(i + 1, j + 1);
    const float top = n00 + (n10 - n00) * fx;
    const float bottom = n01 + (n11 - n01) * fx;
    return top + (bottom - top) * fy;
}

Vec2 Terrain::gradient(Vec2 p) const {
    // Analytic derivative of the bilinear interpolation above.
    const float u = std::clamp(p.x / cell_, 0.0f, static_cast<float>(w_));
    const float v = std::clamp(p.y / cell_, 0.0f, static_cast<float>(h_));
    const int i = std::min(static_cast<int>(u), w_ - 1);
    const int j = std::min(static_cast<int>(v), h_ - 1);
    const float fx = u - static_cast<float>(i);
    const float fy = v - static_cast<float>(j);
    const float n00 = nodeDistance(i, j), n10 = nodeDistance(i + 1, j);
    const float n01 = nodeDistance(i, j + 1), n11 = nodeDistance(i + 1, j + 1);
    const float gx = ((n10 - n00) * (1.0f - fy) + (n11 - n01) * fy) / cell_;
    const float gy = ((n01 - n00) * (1.0f - fx) + (n11 - n10) * fx) / cell_;
    return {gx, gy};
}

bool Terrain::segmentHitsSolid(Vec2 a, Vec2 b, float& tHit) const {
    int x = cellX(a.x);
    int y = cellY(a.y);
    if (isSolid(at(x, y))) { tHit = 0.0f; return true; }

    const Vec2 d = b - a;
    constexpr float inf = std::numeric_limits<float>::infinity();
    const int stepX = d.x > 0.0f ? 1 : -1;
    const int stepY = d.y > 0.0f ? 1 : -1;
    // t at which the segment crosses the next vertical / horizontal cell
    // line, and how much t advances per whole cell.
    const float nextX = static_cast<float>(x + (stepX > 0 ? 1 : 0)) * cell_;
    const float nextY = static_cast<float>(y + (stepY > 0 ? 1 : 0)) * cell_;
    float tMaxX = d.x != 0.0f ? (nextX - a.x) / d.x : inf;
    float tMaxY = d.y != 0.0f ? (nextY - a.y) / d.y : inf;
    const float tDeltaX = d.x != 0.0f ? cell_ / std::abs(d.x) : inf;
    const float tDeltaY = d.y != 0.0f ? cell_ / std::abs(d.y) : inf;

    // Step one axis at a time. Exactly through a corner we still visit one of
    // the two diagonal cells, so there is no gap to slip through.
    while (true) {
        float t;
        if (tMaxX < tMaxY) { t = tMaxX; x += stepX; tMaxX += tDeltaX; }
        else { t = tMaxY; y += stepY; tMaxY += tDeltaY; }
        if (t > 1.0f) return false;
        if (isSolid(at(x, y))) { tHit = std::max(t, 0.0f); return true; }
    }
}

} // namespace spillway
