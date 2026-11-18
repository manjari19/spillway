#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "assets.h"
#include "ease.h"
#include "gameplay_scene.h"
#include "menu_scene.h"
#include "theme.h"
#include "widgets.h"

namespace {

// Shown instead of the game when assets fail to load.
class ErrorScene : public Scene {
public:
    explicit ErrorScene(std::string msg) : msg_(std::move(msg)) {}
    void update(float) override {}
    void drawWorld() override {}
    void drawUi() override {
        const float s = ui::scale();
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), theme::bgBottom);
        DrawText("Spillway could not start", static_cast<int>(40 * s), static_cast<int>(40 * s), static_cast<int>(30 * s), theme::danger);
        DrawText(msg_.c_str(), static_cast<int>(40 * s), static_cast<int>(90 * s), static_cast<int>(18 * s), theme::text);
    }

private:
    std::string msg_;
};

} // namespace

std::string App::levelPath(int index) const {
    char name[32];
    std::snprintf(name, sizeof name, "level%02d.txt", index + 1);
    return assets().levelDir + name;
}

bool App::levelExists(int index) const { return index >= 0 && FileExists(levelPath(index).c_str()); }

std::unique_ptr<Scene> App::makeMenu() { return std::make_unique<MenuScene>(*this); }

std::unique_ptr<Scene> App::makeLevel(int index) {
    auto result = spillway::loadLevelFile(levelPath(index));
    if (!result) {
        std::fprintf(stderr, "%s\n", result.error.c_str());
        return std::make_unique<ErrorScene>(result.error);
    }
    return std::make_unique<GameplayScene>(*this, index, *result.level);
}

int App::run() {
    // Developer hooks for checking layouts without clicking around:
    //   SPILLWAY_SCREENSHOT=/path/out.png   save one frame (~2 s in) and quit
    //   SPILLWAY_SIZE=1440x900              window size
    //   SPILLWAY_SHOT_SCENE=menu|game|pause|complete
    //   SPILLWAY_MOUSE=640,360              pin the mouse (logical px)
    //   SPILLWAY_DEBUG=1                    show the debug overlay
    const char* shotPath = std::getenv("SPILLWAY_SCREENSHOT");
    const std::string shotScene = std::getenv("SPILLWAY_SHOT_SCENE") ? std::getenv("SPILLWAY_SHOT_SCENE") : "menu";
    int width = 1280, height = 720;
    if (const char* size = std::getenv("SPILLWAY_SIZE")) std::sscanf(size, "%dx%d", &width, &height);
    bool pinMouse = false;
    Vector2 pinned{};
    if (const char* m = std::getenv("SPILLWAY_MOUSE")) pinMouse = std::sscanf(m, "%f,%f", &pinned.x, &pinned.y) == 2;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(width, height, "Spillway");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL); // Esc is the pause key
    SetTargetFPS(144);

    std::string error;
    if (!assets().load(error)) {
        std::fprintf(stderr, "Spillway: %s\n", error.c_str());
        scene_ = std::make_unique<ErrorScene>(error);
    }
    renderer_.init();
    if (shotPath) std::printf("screen %dx%d render %dx%d dpi %.2f\n", GetScreenWidth(), GetScreenHeight(),
                              GetRenderWidth(), GetRenderHeight(), renderer_.dpi());

    if (!scene_) {
        if (shotScene == "menu") {
            scene_ = makeMenu();
        } else {
            scene_ = makeLevel(0);
            if (auto* g = dynamic_cast<GameplayScene*>(scene_.get())) {
                if (shotScene == "pause") g->debugPause();
                if (shotScene == "complete") g->debugAutoSolve();
                if (std::getenv("SPILLWAY_DEBUG")) g->debugOverlay(true);
            }
        }
    }

    int frame = 0;
    while (!WindowShouldClose() && !quit_) {
        renderer_.ensureSize();
        if (pinMouse) SetMousePosition(static_cast<int>(pinned.x), static_cast<int>(pinned.y));
        const float dt = std::min(GetFrameTime(), 1.0f / 20.0f);

        scene_->update(dt);
        scene_->prepare();
        renderer_.beginWorld();
        scene_->drawWorld();
        renderer_.endWorld(scene_->wantsBackdrop());
        renderer_.beginUi();
        scene_->drawUi();
        renderer_.endUi();

        fade_ = std::max(0.0f, fade_ - dt / 0.5f);
        renderer_.present(ease::inOutCubic(fade_));

        if (pending_) {
            renderer_.captureSnapshot();
            scene_ = std::move(pending_);
            fade_ = 1.0f;
        }

        if (shotPath && ++frame == 150) {
            Image img = renderer_.screenshot();
            ExportImage(img, shotPath);
            UnloadImage(img);
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
