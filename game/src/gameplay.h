#pragma once

#include <raylib.h>

#include <memory>
#include <string>

#include "spillway/world.h"

// One playable level: input -> queued edits -> World, plus drawing and HUD.
class Gameplay {
public:
    explicit Gameplay(spillway::LevelDesc level);
    ~Gameplay();
    Gameplay(const Gameplay&) = delete;
    Gameplay& operator=(const Gameplay&) = delete;

    void update(float frameSeconds);
    void draw();
    void setDebugOverlay(bool on);

private:
    struct View {
        float scale = 1.0f;
        Vector2 offset{};
        Vector2 toScreen(spillway::Vec2 p) const { return {offset.x + p.x * scale, offset.y + p.y * scale}; }
        spillway::Vec2 toWorld(Vector2 s) const { return {(s.x - offset.x) / scale, (s.y - offset.y) / scale}; }
    };

    void restart();
    void fitView();
    void handleInput();
    void refreshTerrainTexture();
    float& brushRadius();
    bool waterUnderBrush() const;

    void drawWorld();
    void drawBrush();
    void drawHud();
    void drawDebug();
    void drawOutcome(float topY);

    spillway::LevelDesc level_;
    std::unique_ptr<spillway::World> world_;
    View view_;

    spillway::Tool tool_ = spillway::Tool::Dig;
    float digRadius_ = 0.35f;
    float wallRadius_ = 0.1f;
    float eraseRadius_ = 0.35f;
    bool stroking_ = false;
    spillway::Vec2 lastMouse_{};

    bool paused_ = false;
    bool showDebug_ = false;
    bool showGrid_ = false;
    bool debugPush_ = false; // P: pull/push water with the mouse (debug only)
    spillway::Interaction interaction_{};

    Texture2D terrainTex_{};
    Image terrainImg_{};
    uint64_t terrainVersion_ = ~0ull;
    int lastEditSdfNodes_ = 0;
    float meterBottom_ = 0.0f;
};
