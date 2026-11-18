// Spillway - Milestone 2: one playable level.

#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "gameplay.h"
#include "ui.h"

namespace {

// Levels are copied next to the executable at build time. Fall back to the
// source tree so running from an IDE also works.
spillway::LevelLoadResult loadLevel(const std::string& file) {
    auto result = spillway::loadLevelFile(std::string(GetApplicationDirectory()) + "levels/" + file);
    if (!result) {
        auto fallback = spillway::loadLevelFile(std::string(SPILLWAY_SOURCE_LEVEL_DIR) + "/" + file);
        if (fallback) return fallback;
    }
    return result;
}

} // namespace

int main() {
    // Developer hook: SPILLWAY_SCREENSHOT=/path/out.png renders ~2 seconds
    // at SPILLWAY_SIZE (e.g. 1920x1080), saves one frame and quits. Used to
    // check layouts at different resolutions without clicking around.
    const char* shotPath = std::getenv("SPILLWAY_SCREENSHOT");
    int width = 1280, height = 720;
    if (const char* size = std::getenv("SPILLWAY_SIZE")) std::sscanf(size, "%dx%d", &width, &height);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(width, height, "Spillway");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL); // Esc pauses instead of quitting
    SetTargetFPS(144);

    const auto level = loadLevel("level01.txt");
    std::unique_ptr<Gameplay> game;
    if (level) game = std::make_unique<Gameplay>(*level.level);
    if (game && shotPath && std::getenv("SPILLWAY_DEBUG")) game->setDebugOverlay(true);

    int frame = 0;
    while (!WindowShouldClose()) {
        if (game) game->update(GetFrameTime());

        BeginDrawing();
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0x0b, 0x0a, 0x1a, 255},
                               Color{0x05, 0x04, 0x0d, 255});
        if (game) {
            game->draw();
        } else {
            const float s = ui::scale();
            ui::textPanel({{"Could not load level", 28.0f * s, ui::colors::bad}, {level.error, 16.0f * s, ui::colors::dim}},
                          ui::Anchor::Center, 0.0f);
        }
        EndDrawing();

        if (shotPath && ++frame == 120) {
            Image img = LoadImageFromScreen();
            ExportImage(img, shotPath);
            UnloadImage(img);
            break;
        }
    }

    game.reset(); // free GPU resources before the context goes away
    CloseWindow();
    return 0;
}
