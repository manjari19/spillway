#include "ui.h"

#include <algorithm>

namespace ui {

float scale() {
    const float sx = static_cast<float>(GetScreenWidth()) / 1280.0f;
    const float sy = static_cast<float>(GetScreenHeight()) / 720.0f;
    return std::max(0.5f, std::min(sx, sy));
}

// raylib's default font uses spacing = size / 10; keep measuring and drawing
// consistent by always passing the same spacing.
Vector2 measureText(const std::string& text, float size) {
    return MeasureTextEx(GetFontDefault(), text.c_str(), size, size / 10.0f);
}

void drawText(const std::string& text, Vector2 pos, float size, Color color) {
    DrawTextEx(GetFontDefault(), text.c_str(), pos, size, size / 10.0f, color);
}

Vector2 measureLines(const std::vector<Line>& lines, float gap) {
    Vector2 total{0, 0};
    for (size_t i = 0; i < lines.size(); ++i) {
        const Vector2 m = measureText(lines[i].text, lines[i].size);
        total.x = std::max(total.x, m.x);
        total.y += m.y + (i + 1 < lines.size() ? gap : 0.0f);
    }
    return total;
}

void drawLines(const std::vector<Line>& lines, Vector2 topLeft, float gap) {
    float y = topLeft.y;
    for (const Line& l : lines) {
        drawText(l.text, {topLeft.x, y}, l.size, l.color);
        y += measureText(l.text, l.size).y + gap;
    }
}

Rectangle place(Vector2 size, Anchor anchor, float margin) {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    switch (anchor) {
    case Anchor::TopLeft: return {margin, margin, size.x, size.y};
    case Anchor::TopRight: return {W - margin - size.x, margin, size.x, size.y};
    case Anchor::TopCenter: return {(W - size.x) * 0.5f, margin, size.x, size.y};
    case Anchor::BottomLeft: return {margin, H - margin - size.y, size.x, size.y};
    case Anchor::BottomRight: return {W - margin - size.x, H - margin - size.y, size.x, size.y};
    case Anchor::Center: return {(W - size.x) * 0.5f, (H - size.y) * 0.5f, size.x, size.y};
    }
    return {0, 0, size.x, size.y};
}

void drawPanel(Rectangle r) {
    const float radius = std::min(1.0f, 24.0f * scale() / std::min(r.width, r.height));
    DrawRectangleRounded(r, radius, 12, colors::panel);
    DrawRectangleRoundedLinesEx(r, radius, 12, 1.0f, colors::border);
}

Rectangle textPanel(const std::vector<Line>& lines, Anchor anchor, float margin, Vector2 offset) {
    const float s = scale();
    const float pad = 16.0f * s;
    const float gap = 6.0f * s;
    const Vector2 content = measureLines(lines, gap);
    Rectangle r = place({content.x + 2 * pad, content.y + 2 * pad}, anchor, margin);
    r.x += offset.x;
    r.y += offset.y;
    drawPanel(r);
    drawLines(lines, {r.x + pad, r.y + pad}, gap);
    return r;
}

void drawBar(Rectangle r, float fill, Color fillColor, float marker) {
    fill = std::clamp(fill, 0.0f, 1.0f);
    DrawRectangleRounded(r, 1.0f, 8, Color{255, 255, 255, 22});
    if (fill > 0.0f) {
        const Rectangle f{r.x, r.y, std::max(r.height, r.width * fill), r.height};
        DrawRectangleRounded(f, 1.0f, 8, fillColor);
    }
    if (marker >= 0.0f) {
        const float x = r.x + r.width * std::clamp(marker, 0.0f, 1.0f);
        DrawRectangleRec({x - 1.5f, r.y - r.height * 0.5f, 3.0f, r.height * 2.0f}, colors::text);
    }
}

} // namespace ui
