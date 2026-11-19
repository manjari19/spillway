#pragma once

// Where progress and settings live, and reading/writing them.
// macOS: ~/Library/Application Support/Spillway/
// Linux: $XDG_DATA_HOME/Spillway or ~/.local/share/Spillway/
// Windows: %APPDATA%\Spillway\
// SPILLWAY_SAVE_DIR overrides it (used for automated captures).
// A missing or corrupt file just means starting fresh.

#include <string>

#include "spillway/progress.h"

struct Settings {
    bool bloom = true;
    int quality = 1; // 0 Low, 1 Medium, 2 High
    bool showFps = false;
    bool fullscreen = false;
    float particleScale() const { return quality == 0 ? 0.6f : quality == 2 ? 1.4f : 1.0f; }
};

std::string saveDirectory(); // created if missing; empty if impossible
spillway::Progress loadProgress();
void saveProgress(const spillway::Progress& p);
Settings loadSettings();
void saveSettings(const Settings& s);
