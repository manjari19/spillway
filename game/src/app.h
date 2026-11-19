#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "renderer.h"
#include "save.h"
#include "scene.h"
#include "spillway/level.h"
#include "spillway/progress.h"

// Owns the window loop, renderer, current scene, progress and settings.
// Scene changes are requested with goTo() (or the open* helpers) and happen
// at the end of the frame with a cross-fade.
class App {
public:
    int run();

    void goTo(std::unique_ptr<Scene> next) { pending_ = std::move(next); }
    void quit() { quit_ = true; }
    Renderer& renderer() { return renderer_; }

    // Navigation.
    void openMenu();
    void openLevelSelect();
    void openSettings();
    void startLevel(int index); // 0-based

    // Levels: level01.txt, level02.txt, ... found at startup.
    int levelCount() const { return static_cast<int>(levels_.size()); }
    bool levelExists(int index) const { return index >= 0 && index < levelCount(); }
    const spillway::LevelDesc& level(int index) const { return levels_[static_cast<size_t>(index)]; }

    // Progress: best stars / % per level. record() saves to disk and returns
    // true for a new best.
    const spillway::Progress& progress() const { return progress_; }
    bool record(int level, int stars, int percent);
    void resetProgress();

    Settings& settings() { return settings_; }
    void applySettings(); // push settings to the renderer/window and save them

private:
    void loadLevels();

    Renderer renderer_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pending_;
    bool quit_ = false;
    float fade_ = 1.0f; // 1 = showing only the previous frame
    std::vector<spillway::LevelDesc> levels_;
    spillway::Progress progress_;
    Settings settings_;
    bool fullscreenApplied_ = false;
};
