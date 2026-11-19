#include "app.h"

#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "assets.h"
#include "ease.h"
#include "gameplay_scene.h"
#include "input.h"
#include "level_art.h"
#include "level_select_scene.h"
#include "menu_scene.h"
#include "platform.h"
#include "settings_scene.h"
#include "spillway/world.h"
#include "terrain_view.h"
#include "theme.h"
#include "widgets.h"

// GLFW is compiled into raylib; we only need one of its functions to read the
// real window size.
extern "C" {
struct GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height);
}

namespace {

// macOS may shrink a window that does not fit on screen while it is being
// created, before raylib starts listening for resizes. raylib then keeps the
// requested size and a band of the window stays unpainted. Re-sync raylib
// with the real size.
void syncWindowSize() {
    GLFWwindow* w = glfwGetCurrentContext();
    if (!w) return;
    int ww = 0, wh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    if (ww > 0 && wh > 0 && (ww != GetScreenWidth() || wh != GetScreenHeight())) SetWindowSize(ww, wh);
}

void saveScreenshot(Renderer& r, const std::string& path) {
    Image img = r.screenshot();
    ExportImage(img, path.c_str());
    UnloadImage(img);
}

} // namespace

void App::loadLevels() {
    levels_.clear();
    for (int i = 1; i <= 99; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "level%02d.txt", i);
        const std::string path = assets().levelDir + name;
        if (!FileExists(path.c_str())) break;
        auto r = spillway::loadLevelFile(path);
        if (!r) {
            std::fprintf(stderr, "Spillway: %s\n", r.error.c_str());
            break;
        }
        levels_.push_back(*r.level);
    }
    progress_.ensureSize(levelCount());
}

const Texture2D& App::levelThumbnail(int index) {
    // 16:9 at 640x360: sharp at the tooltip's size on a 2x display.
    constexpr int kW = 640, kH = 360;
    if (thumbs_.size() < levels_.size()) thumbs_.resize(levels_.size());
    RenderTexture2D& t = thumbs_[static_cast<size_t>(index)];
    if (!t.id) {
        t = LoadRenderTexture(kW, kH);
        SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
        const spillway::LevelDesc& lv = level(index);
        // The level as it starts, after a second of settling (the spawn
        // lattice relaxes into a natural pool, as the player first sees it).
        spillway::World world(lv);
        for (int k = 0; k < 60; ++k) world.step();
        TerrainView terrain;
        terrain.reset(world.terrain());
        const View view = View::fit(lv.size, {0, 0, static_cast<float>(kW), static_cast<float>(kH)});
        renderer_.renderOffscreen(
            t, [&] { terrain.prepare(renderer_, view); },
            [&] {
                drawDrainsAndPits(lv, view, 0.0f, 0.5f);
                renderer_.drawWater(world.fluid().positions(), world.fluid().velocities(), view);
                terrain.draw(renderer_, view, world.terrain());
            });
        // Developer hook: SPILLWAY_THUMB_DIR=/dir writes each thumbnail as a PNG.
        if (const char* dir = std::getenv("SPILLWAY_THUMB_DIR")) {
            Image img = LoadImageFromTexture(t.texture);
            ImageFlipVertical(&img);
            ExportImage(img, (std::string(dir) + "/thumb" + std::to_string(index + 1) + ".png").c_str());
            UnloadImage(img);
        }
    }
    return t.texture;
}

void App::openMenu() { goTo(std::make_unique<MenuScene>(*this)); }
void App::openLevelSelect() { goTo(std::make_unique<LevelSelectScene>(*this)); }
void App::openSettings() { goTo(std::make_unique<SettingsScene>(*this)); }
void App::startLevel(int index) {
    if (levelExists(index)) goTo(std::make_unique<GameplayScene>(*this, index, level(index)));
}

bool App::record(int level, int stars, int percent) {
    const bool best = progress_.record(level, stars, percent);
    saveProgress(progress_);
    return best;
}

void App::resetProgress() {
    progress_ = {};
    progress_.ensureSize(levelCount());
    saveProgress(progress_);
}

void App::applySettings() {
    renderer_.setBloom(settings_.bloom);
    // Fullscreen follows settings_.fullscreen in updateFullscreen().
    saveSettings(settings_);
}

void App::updateFullscreen(double now) {
    // Alt+Enter toggles here. Ctrl+Cmd+F is the system's own shortcut on
    // macOS (the window menu handles it and the change is adopted below);
    // elsewhere the game handles it too.
    const bool alt = input::down(KEY_LEFT_ALT) || input::down(KEY_RIGHT_ALT);
    bool toggle = alt && input::pressed(KEY_ENTER);
    if (toggle) input::consume(KEY_ENTER);
#if !defined(__APPLE__)
    const bool cmd = input::down(KEY_LEFT_SUPER) || input::down(KEY_RIGHT_SUPER);
    const bool ctrl = input::down(KEY_LEFT_CONTROL) || input::down(KEY_RIGHT_CONTROL);
    if (cmd && ctrl && input::pressed(KEY_F)) {
        input::consume(KEY_F);
        toggle = true;
    }
#endif
    if (toggle) {
        settings_.fullscreen = !settings_.fullscreen;
        saveSettings(settings_);
    }

    // On macOS the switch is animated and asynchronous: give a request time
    // to land before acting on the state again.
    const bool actual = platform::isFullscreen();
    const bool settled = now - fullscreenRequestAt_ > 1.5;
    if (actual != fullscreenSeen_) {
        fullscreenSeen_ = actual;
        const platform::Insets in = platform::safeArea();
        int fw = 0, fh = 0;
        if (GLFWwindow* w = glfwGetCurrentContext()) glfwGetFramebufferSize(w, &fw, &fh);
        std::fprintf(stderr, "Spillway: fullscreen %s: window %dx%d, framebuffer %dx%d, monitor %dx%d, safe top %.0f\n",
                     actual ? "on" : "off", GetScreenWidth(), GetScreenHeight(), fw, fh,
                     GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor()), in.top);
        // Changed by the system rather than by us: remember the new choice.
        if (settled && actual != settings_.fullscreen) {
            settings_.fullscreen = actual;
            saveSettings(settings_);
        }
    }
    if (actual != settings_.fullscreen && settled) {
        platform::setFullscreen(settings_.fullscreen);
        fullscreenRequestAt_ = now;
    }
}

int App::run() {
    // Developer hooks:
    //   SPILLWAY_SIZE=1440x900    window size
    //   SPILLWAY_INPUT=...        scripted input (see input.h), incl. screenshots
    //   SPILLWAY_SAVE_DIR=...     use a different save folder
    // The older single-shot hook still works for quick checks:
    //   SPILLWAY_SCREENSHOT=/path.png [SPILLWAY_SHOT_SCENE=menu|game|flow|pause|complete|dig]
    //   [SPILLWAY_SHOT_TIME=seconds] [SPILLWAY_MOUSE=x,y] [SPILLWAY_DEBUG=1]
    const char* shotPath = std::getenv("SPILLWAY_SCREENSHOT");
    const std::string shotScene = std::getenv("SPILLWAY_SHOT_SCENE") ? std::getenv("SPILLWAY_SHOT_SCENE") : "menu";
    int width = 1280, height = 720;
    if (const char* size = std::getenv("SPILLWAY_SIZE")) std::sscanf(size, "%dx%d", &width, &height);
    bool pinMouse = false;
    Vector2 pinned{};
    if (const char* m = std::getenv("SPILLWAY_MOUSE")) pinMouse = std::sscanf(m, "%f,%f", &pinned.x, &pinned.y) == 2;
    if (const char* script = std::getenv("SPILLWAY_INPUT")) input::loadScript(script);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(width, height, "Spillway");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL); // Esc is the pause / back key
    SetTargetFPS(144);
    // Everything here is 2D; triangle winding should never hide geometry.
    rlDisableBackfaceCulling();

    std::string error;
    if (!assets().load(error)) {
        // Without shaders and fonts the render pipeline cannot run, so show
        // the problem on a plain screen with raylib's built-in font.
        std::fprintf(stderr, "Spillway: %s\n", error.c_str());
        int frame = 0;
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(theme::bgBottom);
            DrawText("Spillway could not start", 40, 40, 30, theme::danger);
            DrawText(error.c_str(), 40, 90, 18, theme::text);
            DrawText("Rebuild (cmake --build build) so assets are copied next to the executable.", 40, 120, 18, theme::textDim);
            EndDrawing();
            if (shotPath && ++frame == 30) {
                Image img = LoadImageFromScreen();
                ExportImage(img, shotPath);
                UnloadImage(img);
                break;
            }
        }
        assets().unload();
        CloseWindow();
        return 1;
    }
    renderer_.init();
    loadLevels();
    progress_ = loadProgress();
    progress_.ensureSize(levelCount());
    settings_ = loadSettings();
    applySettings();

    if (shotPath && shotScene != "menu") {
        scene_ = std::make_unique<GameplayScene>(*this, 0, level(0));
        auto* g = static_cast<GameplayScene*>(scene_.get());
        if (shotScene == "pause") g->debugPause();
        if (shotScene == "complete") g->debugAutoSolve();
        if (shotScene == "flow") g->debugMidLevel();
        if (shotScene == "dig") g->debugDigSoon();
        if (std::getenv("SPILLWAY_DEBUG")) g->debugOverlay(true);
        if (pinMouse) g->debugCursor(true);
    } else {
        scene_ = std::make_unique<MenuScene>(*this);
    }

    const double shotTime = std::getenv("SPILLWAY_SHOT_TIME") ? std::atof(std::getenv("SPILLWAY_SHOT_TIME")) : -1.0;
    const double startTime = GetTime();
    int frame = 0;
    while (!WindowShouldClose() && !quit_) {
        input::beginFrame(GetTime() - startTime);
        if (input::quitRequested()) break;
        if (input::takeOsToggle()) platform::setFullscreen(!platform::isFullscreen());
        updateFullscreen(GetTime());
        // Only a normal window needs re-syncing (see syncWindowSize); a
        // fullscreen window is sized by the OS.
        if (!platform::isFullscreen() && GetTime() - fullscreenRequestAt_ > 1.5) syncWindowSize();
        renderer_.ensureSize();
        const platform::Insets safe = platform::safeArea();
        ui::setSafeInsets(safe.top, safe.left, safe.bottom, safe.right);
        Vector2 probe{};
        while (input::takeProbe(probe)) {
            Vector2 got{};
            if (platform::probeCursor(probe, got))
                std::fprintf(stderr, "Spillway: probe cursor at (%.1f, %.1f) -> reads (%.1f, %.1f), window %dx%d, fullscreen %d\n",
                             probe.x, probe.y, got.x, got.y, GetScreenWidth(), GetScreenHeight(), platform::isFullscreen() ? 1 : 0);
        }
        if (pinMouse) SetMousePosition(static_cast<int>(pinned.x), static_cast<int>(pinned.y));
        const float dt = std::min(GetFrameTime(), 1.0f / 20.0f);

        scene_->update(dt);
        const double renderStart = GetTime();
        scene_->prepare();
        renderer_.beginWorld();
        scene_->drawWorld();
        renderer_.endWorld(scene_->wantsBackdrop());
        renderer_.beginUi();
        scene_->drawUi();
        if (settings_.showFps) {
            const float s = ui::scale();
            const std::string fps = std::to_string(GetFPS()) + " fps";
            const Vector2 m = measureText(FontRole::Mono, fps, 12.0f * s);
            drawText(FontRole::Mono, fps, {static_cast<float>(GetScreenWidth()) - m.x - 12.0f * s,
                                           static_cast<float>(GetScreenHeight()) - m.y - 10.0f * s},
                     12.0f * s, theme::textDim);
        }
        renderer_.endUi();
        lastRenderCpuMs_ = (GetTime() - renderStart) * 1000.0;

        fade_ = std::max(0.0f, fade_ - dt / 0.5f);
        renderer_.present(ease::inOutCubic(fade_));

        std::string scriptShot;
        while (input::takeShot(scriptShot)) saveScreenshot(renderer_, scriptShot);

        if (pending_) {
            renderer_.captureSnapshot();
            scene_ = std::move(pending_);
            fade_ = 1.0f;
        }

        ++frame;
        const bool shotDue = shotTime > 0.0 ? GetTime() - startTime >= shotTime : frame == 150;
        if (shotPath && shotDue) {
            saveScreenshot(renderer_, shotPath);
            break;
        }
    }

    scene_.reset();
    pending_.reset();
    for (RenderTexture2D& t : thumbs_)
        if (t.id) UnloadRenderTexture(t);
    renderer_.unload();
    assets().unload();
    CloseWindow();
    return 0;
}
