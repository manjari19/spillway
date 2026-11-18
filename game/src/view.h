#pragma once

#include <raylib.h>

#include <algorithm>

#include "spillway/vec2.h"

// World (metres) <-> screen (logical pixels) mapping.
struct View {
    float scale = 1.0f; // logical px per metre
    Vector2 offset{};

    Vector2 toScreen(spillway::Vec2 p) const { return {offset.x + p.x * scale, offset.y + p.y * scale}; }
    spillway::Vec2 toWorld(Vector2 s) const { return {(s.x - offset.x) / scale, (s.y - offset.y) / scale}; }
    bool operator==(const View& o) const {
        return scale == o.scale && offset.x == o.offset.x && offset.y == o.offset.y;
    }

    // Whole level visible inside `area`, centred.
    static View fit(spillway::Vec2 extent, Rectangle area) {
        View v;
        v.scale = std::min(area.width / extent.x, area.height / extent.y);
        v.offset = {area.x + (area.width - extent.x * v.scale) * 0.5f, area.y + (area.height - extent.y * v.scale) * 0.5f};
        return v;
    }
    // Level fills `area` completely, cropping the overflow (menu backdrop).
    static View cover(spillway::Vec2 extent, Rectangle area) {
        View v;
        v.scale = std::max(area.width / extent.x, area.height / extent.y);
        v.offset = {area.x + (area.width - extent.x * v.scale) * 0.5f, area.y + (area.height - extent.y * v.scale) * 0.5f};
        return v;
    }
};
