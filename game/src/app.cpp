#include "app.h"

#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "assets.h"
#include "ease.h"
#include "gameplay_scene.h"
#include "menu_scene.h"
#include "theme.h"
#include "widgets.h"

// GLFW is compiled into raylib; we only need two of its functions to read the
// real window size.
extern "C" {
struct GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
}

namespace {

// macOS may shrink a window that does not fit on screen while it is being
// created, before raylib starts listening for resizes. raylib then keeps the
// requested size, the viewport no longer matches the framebuffer, and a band
// at the top of the window stays black. Re-sync raylib with the real size.
void syncWindowSize() {
    GLFWwindow* w = glfwGetCurrentContext();
    if (!w) return;
    int ww = 0, wh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    if (ww > 0 && wh > 0 && (ww != GetScreenWidth() || wh != GetScreenHeight())) SetWindowSize(ww, wh);
}

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
    //   SPILLWAY_SHOT_TIME=0.9              capture at this many seconds instead of frame 150
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
                if (shotScene == "flow") g->debugMidLevel();
                if (shotScene == "dig") g->debugDigSoon();
                if (std::getenv("SPILLWAY_DEBUG")) g->debugOverlay(true);
                if (pinMouse) g->debugCursor(true);
            }
        }
    }

    const double shotTime = std::getenv("SPILLWAY_SHOT_TIME") ? std::atof(std::getenv("SPILLWAY_SHOT_TIME")) : -1.0;
    const double startTime = GetTime();
    int frame = 0;
    while (!WindowShouldClose() && !quit_) {
        // SPILLWAY_MAXIMIZE=1: maximise the window shortly after start, to test
        // that resizes never leave unpainted (black) bands.
        if (frame == 20 && std::getenv("SPILLWAY_MAXIMIZE")) MaximizeWindow();
        syncWindowSize();
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

        ++frame;
        const bool shotDue = shotTime > 0.0 ? GetTime() - startTime >= shotTime : frame == 150;
        if (shotPath && shotDue) {
            // SPILLWAY_SHOT_WINDOW=1 reads the real window framebuffer instead
            // of the offscreen frame (to check letterboxing / presentation).
            Image img = renderer_.screenshot();
            if (std::getenv("SPILLWAY_SHOT_WINDOW")) {
                // Read back the real window framebuffer at its true size.
                UnloadImage(img);
                const int w = renderer_.renderWidth(), h = renderer_.renderHeight();
                unsigned char* px = rlReadScreenPixels(w, h);
                img = Image{px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            }
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
