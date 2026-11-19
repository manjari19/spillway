#pragma once

// Level decorations shared by the game and the level thumbnails.

#include "spillway/level.h"
#include "view.h"

// Drains (a glowing grate with light rising out of it) and pits (a dark void
// with a red-violet glow at the lip). `time` drives the pulse; `uiScale`
// sizes the grate bars. Draw before the water and terrain, so buried drains
// stay hidden until dug out.
void drawDrainsAndPits(const spillway::LevelDesc& level, const View& view, float time, float uiScale);
