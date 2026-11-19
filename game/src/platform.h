#pragma once

// The few things raylib doesn't cover, per OS.
//
// Fullscreen: on macOS this is the system's native fullscreen (the green
// button / Ctrl+Cmd+F): the window gets its own Space and fills the display
// at its real Retina resolution. The display mode never changes, so nothing
// is stretched or blurry. Elsewhere it is a borderless window the size of the
// monitor. Either way the game just sees a bigger window and re-lays out.

#include <raylib.h>

namespace platform {

bool isFullscreen();
void setFullscreen(bool on);

// Parts of the window the hardware covers, in logical pixels: on a MacBook
// with a camera notch, the band beside the notch if the fullscreen window
// reaches into it. The background is drawn there, but HUD and menus stay
// below it. (Current macOS places fullscreen windows below the notch, so
// this is usually 0 there.)
struct Insets {
    float top = 0.0f, left = 0.0f, bottom = 0.0f, right = 0.0f;
};
Insets safeArea();

// Developer check of mouse mapping: moves the real OS cursor to window point
// `p` (logical px) using the OS's own screen coordinates, and returns where
// the windowing layer then reports the cursor. Returns false if unsupported.
bool probeCursor(Vector2 p, Vector2& reported);

} // namespace platform
