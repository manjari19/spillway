// Spillway - Milestone 1 sandbox.
// A box of SPH fluid drawn as circles. Mouse stirs the water.

#include <raylib.h>

#include <algorithm>
#include <cstdio>

#include "spillway/simulation.h"

using namespace spillway;

namespace {

constexpr float kWorldW = 16.0f;
constexpr float kWorldH = 9.0f;

// Maps world metres to screen pixels, letterboxed to keep aspect ratio.
struct View {
    float scale = 1.0f;
    Vector2 offset{};

    void fit(int screenW, int screenH) {
        scale = std::min(screenW / kWorldW, screenH / kWorldH);
        offset = {(screenW - kWorldW * scale) * 0.5f, (screenH - kWorldH * scale) * 0.5f};
    }
    Vector2 toScreen(Vec2 p) const { return {offset.x + p.x * scale, offset.y + p.y * scale}; }
    Vec2 toWorld(Vector2 s) const { return {(s.x - offset.x) / scale, (s.y - offset.y) / scale}; }
};

Color lerpColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(x + (y - x) * t);
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

void resetScene(Simulation& sim) {
    sim.fluid().clear();
    sim.fluid().spawnBlock({0.3f, 0.6f}, {7.5f, 8.7f}); // dam break from the left
}

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Spillway");
    SetTargetFPS(144);

    Simulation sim(FluidParams{}, {0, 0}, {kWorldW, kWorldH});
    resetScene(sim);

    View view;
    bool showDebug = true;
    bool showGrid = false;

    const Color slow{0x6d, 0x3c, 0xff, 255}; // deep violet
    const Color fast{0x3e, 0xe8, 0xff, 255}; // bright cyan

    while (!WindowShouldClose()) {
        view.fit(GetScreenWidth(), GetScreenHeight());

        // --- input ---
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;
        if (IsKeyPressed(KEY_G)) showGrid = !showGrid;
        if (IsKeyPressed(KEY_R)) resetScene(sim);
        if (IsKeyPressed(KEY_B)) {
            auto& f = sim.fluid();
            f.setNeighbourSearch(f.neighbourSearch() == NeighbourSearch::Grid ? NeighbourSearch::BruteForce
                                                                               : NeighbourSearch::Grid);
        }

        Interaction interaction;
        interaction.point = view.toWorld(GetMousePosition());
        interaction.radius = 1.6f;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { interaction.active = true; interaction.strength = 40.0f; }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { interaction.active = true; interaction.strength = -40.0f; }
        sim.fluid().setInteraction(interaction);

        // --- physics (fixed timestep, independent of frame rate) ---
        sim.update(GetFrameTime());

        // --- draw ---
        BeginDrawing();
        ClearBackground(Color{0x05, 0x04, 0x0d, 255});
        const Vector2 a = view.toScreen({0, 0});
        DrawRectangleV(a, {kWorldW * view.scale, kWorldH * view.scale}, Color{0x0b, 0x0a, 0x1a, 255});

        if (showGrid) {
            const auto& g = sim.fluid().grid();
            for (int y = 0; y < g.cellsY(); ++y) {
                for (int x = 0; x < g.cellsX(); ++x) {
                    const Vec2 c0 = g.origin() + Vec2{x * g.cellSize(), y * g.cellSize()};
                    const Vector2 s = view.toScreen(c0);
                    const float size = g.cellSize() * view.scale;
                    const unsigned char alpha = static_cast<unsigned char>(std::min(120u, g.cellCount(x, y) * 6u));
                    if (alpha > 0) DrawRectangleV(s, {size, size}, Color{62, 232, 255, alpha});
                    DrawRectangleLinesEx({s.x, s.y, size, size}, 1.0f, Color{255, 255, 255, 12});
                }
            }
        }

        const auto pos = sim.fluid().positions();
        const auto vel = sim.fluid().velocities();
        const float radius = std::max(1.5f, 0.07f * view.scale);
        for (size_t i = 0; i < pos.size(); ++i) {
            DrawCircleV(view.toScreen(pos[i]), radius, lerpColor(slow, fast, length(vel[i]) / 6.0f));
        }

        if (interaction.active) {
            DrawCircleLinesV(GetMousePosition(), interaction.radius * view.scale, Color{255, 255, 255, 60});
        }

        if (showDebug) {
            char buf[256];
            std::snprintf(buf, sizeof buf,
                          "FPS %d\nstep %.2f ms\nparticles %zu\nsearch %s\nthreads 1\n\n"
                          "LMB pull  RMB push\nR reset  B search  G grid  F1 hide",
                          GetFPS(), sim.lastStepMs(), sim.fluid().size(),
                          sim.fluid().neighbourSearch() == NeighbourSearch::Grid ? "grid" : "brute force");
            DrawRectangleRounded({12, 12, 260, 190}, 0.12f, 8, Color{20, 18, 40, 200});
            DrawText(buf, 24, 24, 18, Color{220, 220, 255, 230});
        }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
