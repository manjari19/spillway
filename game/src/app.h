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
#include "spillway/thread_pool.h"

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

    // A picture of a level as it starts, drawn by the real terrain and water
    // shaders. Made on first use and kept for the session.
    const Texture2D& levelThumbnail(int index);
    bool hasLevelThumbnail(int index) const {
        return index >= 0 && index < static_cast<int>(thumbs_.size()) && thumbs_[static_cast<size_t>(index)].id != 0;
    }

    // How far the level path's water reached when it was last shown (a level
    // index; -1 = not shown yet this session). Returning to the path after a
    // win animates the water from there to the new level.
    int pathFillShown() const { return pathFillShown_; }
    void setPathFillShown(int fill) { pathFillShown_ = fill; }

    Settings& settings() { return settings_; }

    // One thread per hardware thread, shared by every level's simulation.
    spillway::ThreadPool& simPool() { return simPool_; }
    // CPU time spent building last frame's draw calls (scene prepare through
    // UI), in ms. The GPU works asynchronously, so this is CPU cost only.
    double lastRenderCpuMs() const { return lastRenderCpuMs_; }
    void applySettings(); // push settings to the renderer/window and save them

private:
    void loadLevels();
    // Keeps the window's fullscreen state and the setting in agreement:
    // handles Alt+Enter, adopts changes made by the system (green button,
    // Ctrl+Cmd+F from the window menu), and requests the wanted state.
    void updateFullscreen(double now);

    Renderer renderer_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pending_;
    bool quit_ = false;
    float fade_ = 1.0f; // 1 = showing only the previous frame
    std::vector<spillway::LevelDesc> levels_;
    spillway::Progress progress_;
    Settings settings_;
    double fullscreenRequestAt_ = -10.0; // when we last asked the OS to switch
    bool fullscreenSeen_ = false;        // the window's state last frame
    std::vector<RenderTexture2D> thumbs_;
    int pathFillShown_ = -1;
    spillway::ThreadPool simPool_{spillway::ThreadPool::hardwareThreads()};
    double lastRenderCpuMs_ = 0.0;
};
