#include "widgets.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "ease.h"
#include "theme.h"

namespace ui {

float scale() {
    const float sx = static_cast<float>(GetScreenWidth()) / 1280.0f;
    const float sy = static_cast<float>(GetScreenHeight()) / 720.0f;
    return std::max(0.6f, std::min(sx, sy));
}

float roundness(Rectangle r, float radius) {
    const float m = std::min(r.width, r.height);
    return m > 0.0f ? std::clamp(2.0f * radius / m, 0.0f, 1.0f) : 0.0f;
}

float animate(const std::string& id, float target, float rate) {
    // std::map, not unordered_map: deterministic and plenty fast for a few
    // dozen UI ids.
    static std::map<std::string, float> values;
    auto [it, inserted] = values.try_emplace(id, target);
    it->second = ease::approach(it->second, target, rate, GetFrameTime());
    return it->second;
}

void glass(Rectangle r, float radius, float alpha) {
    const float s = scale();
    // Soft shadow: a few expanding, fading rounded rects offset downward.
    for (int i = 6; i >= 1; --i) {
        const float g = static_cast<float>(i) * 3.0f * s;
        const Rectangle sr{r.x - g, r.y - g + 8.0f * s, r.width + 2 * g, r.height + 2 * g};
        DrawRectangleRounded(sr, roundness(sr, radius + g), 16, ease::alpha(Color{0, 0, 0, 255}, alpha * 0.05f));
    }
    DrawRectangleRounded(r, roundness(r, radius), 24, ease::alpha(theme::glassFill, alpha));
    // Sheen: a lighter rounded rect over the top part.
    const Rectangle top{r.x + 1, r.y + 1, r.width - 2, r.height * 0.5f};
    DrawRectangleRounded(top, roundness(top, radius), 24, ease::alpha(theme::glassSheen, alpha));
    DrawRectangleRoundedLinesEx(r, roundness(r, radius), 24, 1.0f, ease::alpha(theme::glassBorder, alpha));
}

void textCentered(FontRole role, const std::string& text, Rectangle r, float size, Color color, float tracking) {
    const Vector2 m = measureText(role, text, size, tracking);
    drawText(role, text, {std::round(r.x + (r.width - m.x) * 0.5f), std::round(r.y + (r.height - m.y) * 0.5f)}, size,
             color, tracking);
}

ButtonResult button(const std::string& id, Rectangle r, const std::string& label, bool focused, bool activate,
                    float alpha, bool primary, bool enabled) {
    const float s = scale();
    ButtonResult res;
    res.hovered = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    const bool hot = enabled && (res.hovered || focused);
    const float h = animate("btn:" + id, hot ? 1.0f : 0.0f);

    // Hover: scale up ~3% around the centre.
    const float k = 1.0f + 0.03f * h;
    const Rectangle b{r.x + r.width * (1 - k) * 0.5f, r.y + r.height * (1 - k) * 0.5f, r.width * k, r.height * k};
    const float radius = b.height * 0.5f;

    // Glow halo when hot.
    if (h > 0.01f) {
        for (int i = 3; i >= 1; --i) {
            const float g = static_cast<float>(i) * 4.0f * s;
            const Rectangle gr{b.x - g, b.y - g, b.width + 2 * g, b.height + 2 * g};
            DrawRectangleRounded(gr, 1.0f, 24, ease::alpha(theme::violet, alpha * h * 0.07f));
        }
    }
    Color fill = primary ? ease::mix(Color{92, 52, 220, 220}, Color{118, 76, 255, 240}, h)
                         : ease::mix(Color{255, 255, 255, 16}, Color{255, 255, 255, 34}, h);
    if (!enabled) fill = Color{255, 255, 255, 8};
    DrawRectangleRounded(b, roundness(b, radius), 24, ease::alpha(fill, alpha));
    DrawRectangleRoundedLinesEx(b, roundness(b, radius), 24, 1.0f,
                                ease::alpha(Color{255, 255, 255, static_cast<unsigned char>(30 + 40 * h)}, alpha));
    const Color tc = !enabled ? theme::textMuted : ease::mix(primary ? theme::text : theme::textDim, theme::text, h);
    textCentered(FontRole::Medium, label, b, theme::type::body * s, ease::alpha(tc, alpha));

    res.clicked = enabled && ((res.hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (focused && activate));
    return res;
}

void waterBar(Rectangle r, float fill, float target, float alpha) {
    fill = std::clamp(fill, 0.0f, 1.0f);
    const float rad = r.height * 0.5f;
    DrawRectangleRounded(r, 1.0f, 16, ease::alpha(Color{255, 255, 255, 18}, alpha));
    if (fill > 0.001f) {
        // Gradient body between two round caps. The colour at the head of
        // the fill moves from violet toward cyan as it grows.
        const float w = std::max(r.height, r.width * fill);
        const Color head = ease::mix(theme::waterSlow, theme::waterFast, fill);
        const Color tail = ease::alpha(theme::waterSlow, alpha);
        const Color headA = ease::alpha(head, alpha);
        DrawCircleV({r.x + rad, r.y + rad}, rad, tail);
        DrawCircleV({r.x + w - rad, r.y + rad}, rad, headA);
        if (w > 2 * rad)
            DrawRectangleGradientEx({r.x + rad, r.y, w - 2 * rad, r.height}, tail, tail, headA, headA);
        // Soft glow over the fill.
        DrawRectangleRounded({r.x, r.y - rad * 0.6f, w, r.height + rad * 1.2f}, 1.0f, 16,
                             ease::alpha(head, alpha * 0.12f));
    }
    if (target >= 0.0f) {
        const float x = r.x + r.width * std::clamp(target, 0.0f, 1.0f);
        const float th = r.height * 0.9f;
        const Rectangle tick{x - 1.5f * scale(), r.y - th * 0.55f, 3.0f * scale(), r.height + th * 1.1f};
        DrawRectangleRounded(tick, 1.0f, 8, ease::alpha(theme::text, alpha * 0.9f));
    }
}

void plainBar(Rectangle r, float fill, Color color, float alpha) {
    fill = std::clamp(fill, 0.0f, 1.0f);
    DrawRectangleRounded(r, 1.0f, 16, ease::alpha(Color{255, 255, 255, 18}, alpha));
    if (fill > 0.001f) {
        const Rectangle f{r.x, r.y, std::max(r.height, r.width * fill), r.height};
        DrawRectangleRounded(f, 1.0f, 16, ease::alpha(color, alpha));
    }
}

void star(Vector2 c, float radius, float fill, float alpha) {
    // 10 alternating outer/inner points, drawn as a triangle fan.
    Vector2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -1.5708f + static_cast<float>(i) * 3.14159265f / 5.0f;
        const float rr = (i % 2 == 0) ? radius : radius * 0.46f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    rlDisableBackfaceCulling();
    if (fill > 0.01f) {
        // Glow behind a lit star.
        const Texture2D& blob = assets().blob;
        const float g = radius * 3.2f;
        DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                       {c.x - g, c.y - g, 2 * g, 2 * g}, {0, 0}, 0.0f, ease::alpha(theme::cyan, alpha * fill * 0.35f));
    }
    const Color empty = Color{255, 255, 255, 26};
    const Color col = ease::alpha(ease::mix(empty, theme::star, fill), alpha);
    for (int i = 0; i < 10; ++i) DrawTriangle(c, pts[i], pts[(i + 1) % 10], col);
    if (fill < 0.99f)
        for (int i = 0; i < 10; ++i)
            DrawLineEx(pts[i], pts[(i + 1) % 10], 1.2f * scale(), ease::alpha(Color{255, 255, 255, 50}, alpha * (1 - fill)));
    rlEnableBackfaceCulling();
}

void toolIcon(spillway::Tool tool, Vector2 c, float size, Color color) {
    const float s = size;
    const float w = std::max(1.5f, s * 0.11f);
    switch (tool) {
    case spillway::Tool::Dig: {
        // A downward chevron above a ground line: "dig down".
        DrawLineEx({c.x - s * 0.3f, c.y - s * 0.22f}, {c.x, c.y + s * 0.08f}, w, color);
        DrawLineEx({c.x + s * 0.3f, c.y - s * 0.22f}, {c.x, c.y + s * 0.08f}, w, color);
        DrawCircleV({c.x - s * 0.3f, c.y - s * 0.22f}, w * 0.5f, color);
        DrawCircleV({c.x + s * 0.3f, c.y - s * 0.22f}, w * 0.5f, color);
        DrawCircleV({c.x, c.y + s * 0.08f}, w * 0.5f, color);
        DrawLineEx({c.x - s * 0.34f, c.y + s * 0.3f}, {c.x + s * 0.34f, c.y + s * 0.3f}, w, color);
        DrawCircleV({c.x - s * 0.34f, c.y + s * 0.3f}, w * 0.5f, color);
        DrawCircleV({c.x + s * 0.34f, c.y + s * 0.3f}, w * 0.5f, color);
        break;
    }
    case spillway::Tool::Wall: {
        // A thick stroke with round caps.
        const Vector2 a{c.x - s * 0.3f, c.y + s * 0.22f}, b{c.x + s * 0.3f, c.y - s * 0.22f};
        DrawLineEx(a, b, w * 2.0f, color);
        DrawCircleV(a, w, color);
        DrawCircleV(b, w, color);
        break;
    }
    case spillway::Tool::Erase: {
        // A dashed ring with a small cross.
        for (int i = 0; i < 8; ++i) {
            const float a0 = static_cast<float>(i) * 45.0f;
            DrawRing(c, s * 0.34f - w * 0.5f, s * 0.34f + w * 0.5f, a0, a0 + 26.0f, 6, color);
        }
        DrawLineEx({c.x - s * 0.12f, c.y - s * 0.12f}, {c.x + s * 0.12f, c.y + s * 0.12f}, w, color);
        DrawLineEx({c.x + s * 0.12f, c.y - s * 0.12f}, {c.x - s * 0.12f, c.y + s * 0.12f}, w, color);
        break;
    }
    }
}

} // namespace ui
