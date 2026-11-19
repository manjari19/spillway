#include "level_art.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

#include "ease.h"
#include "theme.h"

using namespace spillway;

void drawDrainsAndPits(const LevelDesc& level, const View& view_, float time_, float uiScale) {
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.4f);
    for (const Rect& d : level.drains) {
        const Vector2 a = view_.toScreen(d.min), b = view_.toScreen(d.max);
        const Rectangle r{a.x, a.y, b.x - a.x, b.y - a.y};
        // Light rising out of the grate, fading upward (hidden under dirt
        // until the player digs down to it).
        const float glowH = r.height * 2.2f;
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y - glowH), static_cast<int>(r.width),
                               static_cast<int>(glowH), Color{62, 232, 255, 0},
                               ease::alpha(theme::cyan, 0.16f + 0.08f * pulse));
        // The portal itself: a bright body with grate bars.
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                               static_cast<int>(r.height), ease::alpha(theme::cyan, 0.55f + 0.2f * pulse),
                               ease::alpha(theme::violet, 0.35f));
        const int bars = std::max(3, static_cast<int>(r.width / (14.0f * uiScale)));
        for (int i = 1; i < bars; ++i) {
            const float x = r.x + r.width * static_cast<float>(i) / static_cast<float>(bars);
            DrawLineEx({x, r.y + 3}, {x, r.y + r.height}, 2.0f * uiScale, Color{10, 8, 30, 150});
        }
        DrawRectangleRec({r.x, r.y, r.width, 2.5f * uiScale}, ease::alpha(theme::wall, 0.75f + 0.25f * pulse));
    }
    for (const Rect& p : level.pits) {
        const Vector2 a = view_.toScreen(p.min), b = view_.toScreen(p.max);
        const Rectangle r{a.x, a.y, b.x - a.x, b.y - a.y};
        // Dark void with a faint red-violet glow at its lip.
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                               static_cast<int>(r.height), ease::alpha(theme::danger, 0.28f + 0.08f * pulse),
                               Color{3, 2, 8, 255});
        const float glowH = r.height * 0.8f;
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y - glowH), static_cast<int>(r.width),
                               static_cast<int>(glowH), Color{217, 74, 154, 0}, ease::alpha(theme::danger, 0.12f));
    }
}

