#pragma once

#include <memory>
#include <string>
#include <vector>

#include "scene.h"
#include "spillway/world.h"
#include "terrain_view.h"
#include "view.h"

class App;

// Title screen: a living spillway fills the right of the screen behind a
// left-aligned menu. A stream pours from the cliff into a basin, over a dam
// and into a drain; drained water is recycled to the outlet, so it runs
// forever. The mouse can stir the water.
class MenuScene : public Scene {
public:
    explicit MenuScene(App& app);
    void update(float dt) override;
    void prepare() override;
    void drawWorld() override;
    void drawUi() override;

private:
    struct Item {
        std::string label;
        bool enabled;
    };
    struct Mote {
        spillway::Vec2 p;
        float phase, speed, size;
    };
    void activate(int index);
    float itemY(int index) const;
    float titleY() const;
    float itemHeight(int index) const;
    void emit();
    void drawTitle(float alpha) const;

    App& app_;
    std::unique_ptr<spillway::World> world_;
    TerrainView terrainView_;
    View view_;
    float time_ = 0.0f;
    size_t targetParticles_ = 0;
    std::vector<Mote> motes_;

    std::vector<Item> items_;
    std::string continueLine_;
    int focus_ = 0;
    float barY_ = -1.0f;
    float intro_ = 0.0f;
    Vector2 lastMouse_{};
    float stir_ = 0.0f;
};
