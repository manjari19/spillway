#pragma once

#include "scene.h"

class App;

// Settings card: bloom, water quality, FPS counter, fullscreen, and reset
// progress (with a confirm step). Changes apply and save immediately.
class SettingsScene : public Scene {
public:
    explicit SettingsScene(App& app);
    void update(float dt) override;
    void drawWorld() override;
    void drawUi() override;

private:
    enum Row { Bloom, Quality, Fps, Fullscreen, Reset, Back, RowCount };
    void change(int row, int dir);

    App& app_;
    int focus_ = 0;
    bool confirmReset_ = false;
    float confirmSince_ = 0.0f; // the key that opened the confirm must not also confirm it
    bool resetDone_ = false;
    float time_ = 0.0f;
};
