#include "app.h"

#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "assets.h"
#include "ease.h"
#include "gameplay_scene.h"
#include "input.h"
#include "level_select_scene.h"
#include "menu_scene.h"
#include "settings_scene.h"
#include "theme.h"
#include "widgets.h"

// GLFW is compiled into raylib; we only need one of its functions to read the
// real window size.
extern "C" {
struct GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
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
    // Borderless fullscreen is the reliable kind on every desktop OS.
    if (settings_.fullscreen != fullscreenApplied_) {
        ToggleBorderlessWindowed();
        fullscreenApplied_ = settings_.fullscreen;
    }
    saveSettings(settings_);
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
        syncWindowSize();
        renderer_.ensureSize();
        input::beginFrame(GetTime() - startTime);
        if (input::quitRequested()) break;
        if (pinMouse) SetMousePosition(static_cast<int>(pinned.x), static_cast<int>(pinned.y));
        const float dt = std::min(GetFrameTime(), 1.0f / 20.0f);

        scene_->update(dt);
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
    renderer_.unload();
    assets().unload();
    CloseWindow();
    return 0;
}
