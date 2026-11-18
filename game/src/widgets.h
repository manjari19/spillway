#pragma once

// Immediate-mode UI pieces in the "soft glass" style. All sizes are logical
// pixels; callers multiply by ui::scale().

#include <raylib.h>

#include <string>

#include "assets.h"
#include "spillway/world.h"

namespace ui {

// UI scale relative to a 1280x720 reference, limited by the tighter axis.
float scale();

// Roundness value for raylib's rounded rectangles from a pixel radius.
float roundness(Rectangle r, float radius);

// Soft glow (or shadow) around a rounded box, centred on it. `inside`
// sets the opacity under the box itself (0 = ring only).
void glow(Rectangle box, float radius, float softness, Color color, float inside);

// Translucent rounded card with a soft drop shadow, thin light border and a
// faint sheen on its upper half.
void glass(Rectangle r, float radius, float alpha = 1.0f);

// Text drawn centred in a rectangle.
void textCentered(FontRole role, const std::string& text, Rectangle r, float size, Color color, float tracking = 0.0f);

// A pill button. Hover/focus animate a gentle scale (~1.03) and glow.
// Returns true when clicked (or activated by `activate` while focused).
// `hovered` reports mouse hover so callers can move keyboard focus.
struct ButtonResult {
    bool clicked = false;
    bool hovered = false;
};
ButtonResult button(const std::string& id, Rectangle r, const std::string& label, bool focused, bool activate,
                    float alpha = 1.0f, bool primary = false, bool enabled = true);

// Rounded track with a violet -> cyan fill and an optional target tick.
void waterBar(Rectangle r, float fill, float target, float alpha = 1.0f);
void plainBar(Rectangle r, float fill, Color color, float alpha = 1.0f);

// Five-point star. `fill` 0..1 blends from an empty outline to a lit star.
void star(Vector2 centre, float radius, float fill, float alpha = 1.0f);

// Minimal vector icons for the three tools.
void toolIcon(spillway::Tool tool, Vector2 centre, float size, Color color);

// Per-id animation values (hover etc.), eased toward a target each frame.
float animate(const std::string& id, float target, float rate = 14.0f);

} // namespace ui
