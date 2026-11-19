#pragma once

#include <raylib.h>

#include <vector>

#include "scene.h"

class App;

// Grid of glass cards, one per level, with a thumbnail of the layout, best
// stars and best %. Locked levels are muted with a padlock.
class LevelSelectScene : public Scene {
public:
    explicit LevelSelectScene(App& app);
    ~LevelSelectScene() override;
    void update(float dt) override;
    void drawWorld() override;
    void drawUi() override;

private:
    Rectangle cardRect(int index) const;
    void open(int index);

    App& app_;
    std::vector<Texture2D> thumbs_;
    int focus_ = 0;
    float time_ = 0.0f;
};
