#pragma once

#include <memory>
#include <string>
#include <vector>

#include "effects.h"
#include "scene.h"
#include "spillway/world.h"
#include "terrain_view.h"
#include "view.h"

class App;

// Title screen: a live fluid basin fills the screen behind a left-aligned
// menu. The mouse stirs the water; gravity tilts slowly so it never rests.
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
    void activate(int index);
    float itemY(int index) const;
    void drawTitle(float alpha) const;

    App& app_;
    std::unique_ptr<spillway::World> world_;
    TerrainView terrainView_;
    View view_;
    float time_ = 0.0f;

    std::vector<Item> items_;
    int focus_ = 0;
    float barY_ = -1.0f;     // eased highlight bar position
    float barAlpha_ = 0.0f;
    float intro_ = 0.0f;     // 0 -> 1 entrance animation
    Vector2 lastMouse_{};
    float stir_ = 0.0f;      // seconds of stirring left
};
