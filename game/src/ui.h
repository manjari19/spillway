#pragma once

// Small immediate-mode UI helpers. Every panel is sized from the measured
// text it contains, so text can never run past its background.

#include <raylib.h>

#include <string>
#include <vector>

namespace ui {

// UI scale relative to a 1280x720 reference, limited by the tighter axis so
// layouts fit at any aspect ratio.
float scale();

struct Line {
    std::string text;
    float size;  // font size in pixels (already scaled)
    Color color;
};

Vector2 measureText(const std::string& text, float size);
void drawText(const std::string& text, Vector2 pos, float size, Color color);

// Size of a block of lines stacked vertically with `gap` between them.
Vector2 measureLines(const std::vector<Line>& lines, float gap);
void drawLines(const std::vector<Line>& lines, Vector2 topLeft, float gap);

enum class Anchor { TopLeft, TopRight, TopCenter, BottomLeft, BottomRight, Center };

// Places a box of `size` against a screen anchor with a margin.
Rectangle place(Vector2 size, Anchor anchor, float margin);

void drawPanel(Rectangle r);

// Measures, places, draws a panel and its text. Returns the panel rect.
Rectangle textPanel(const std::vector<Line>& lines, Anchor anchor, float margin, Vector2 offset = {0, 0});

// Horizontal progress bar with an optional target marker (both in [0, 1]).
void drawBar(Rectangle r, float fill, Color fillColor, float marker = -1.0f);

namespace colors {
inline constexpr Color text{232, 228, 255, 255};
inline constexpr Color dim{160, 154, 200, 255};
inline constexpr Color accent{62, 232, 255, 255};
inline constexpr Color violet{109, 60, 255, 255};
inline constexpr Color panel{22, 19, 44, 215};
inline constexpr Color border{255, 255, 255, 30};
inline constexpr Color good{120, 255, 190, 255};
inline constexpr Color bad{255, 110, 140, 255};
} // namespace colors

} // namespace ui
