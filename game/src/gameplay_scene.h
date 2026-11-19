#pragma once

#include <memory>

#include "effects.h"
#include "scene.h"
#include "spillway/world.h"
#include "terrain_view.h"
#include "view.h"

class App;

// One playable level: input -> queued edits -> World, world rendering, HUD,
// and the pause / result cards.
class GameplayScene : public Scene {
public:
    GameplayScene(App& app, int levelIndex, spillway::LevelDesc level);

    void update(float dt) override;
    void prepare() override;
    void drawWorld() override;
    void drawUi() override;
    bool wantsBackdrop() const override { return card_ > 0.001f; }

    // Developer hooks used by the screenshot mode.
    void debugPause();
    void debugAutoSolve();
    void debugMidLevel();
    void debugDigSoon() { digDemoAt_ = 0.6f; } // scripted dig at 0.6 s (pair with SPILLWAY_SHOT_TIME)
    void debugOverlay(bool on) { showDebug_ = on; }
    void debugCursor(bool on) { forceCursor_ = on; } // pinned mouse counts as on-screen

private:
    enum class Mode { Playing, Paused, Result };

    void layout();
    void handlePlayInput();
    void spawnEffects();
    float& brushRadius();
    bool brushBlocked() const;
    void restart();
    void toMenu();

    void drawDrainsAndPits();
    void drawBrush();
    void drawHud();
    void drawToasts();
    void drawDrainShares();
    void enterResult();
    Rectangle finishButtonRect() const;
    void drawPauseCard();
    void drawResultCard();
    void drawDebug();
    // Shared card animation: fade + scale 0.96 -> 1 around the card centre.
    void beginCard(Rectangle r);
    void endCard();

    App& app_;
    int levelIndex_;
    spillway::LevelDesc level_;
    std::unique_ptr<spillway::World> world_;
    TerrainView terrainView_;
    Effects effects_;
    View view_;
    Rectangle hudBar_{};

    Mode mode_ = Mode::Playing;
    float time_ = 0.0f;
    float card_ = 0.0f;        // card animation 0..1
    float resultTime_ = 0.0f;  // seconds since the result card opened
    int stars_ = 0;
    bool newBest_ = false;      // this result beat the saved record
    bool allComplete_ = false;  // just won the last level with every level beaten
    float particleScale_ = 1.0f;
    int focus_ = 0;            // keyboard focus within a card
    float meter_ = 0.0f;       // eased saved fraction
    float ink_ = 1.0f;         // eased ink fraction

    spillway::Tool tool_ = spillway::Tool::Dig;
    float digRadius_ = 0.35f;
    float wallRadius_ = 0.1f;
    float eraseRadius_ = 0.35f;
    bool stroking_ = false;
    spillway::Vec2 lastMouse_{};

    bool showDebug_ = false;
    bool forceCursor_ = false;
    float digDemoAt_ = -1.0f;
    bool showGrid_ = false;
    bool debugPush_ = false;
    spillway::Interaction interaction_{};
    int lastSdfNodes_ = 0;
    int lastChunks_ = 0;
    double phaseMs_[4] = {0, 0, 0, 0}; // grid, density, forces, integrate (smoothed, per fixed step)
};
