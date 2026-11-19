#pragma once

#include <raylib.h>

#include <string>
#include <vector>

#include "scene.h"
#include "spillway/vec2.h"

class App;

// The level path: a winding river channel across the screen with one node
// per level. Glowing water fills the channel from its spring up to the first
// level not yet beaten (all the way to the end once every level is done),
// and flows forward when you come back after beating a level.
//
// Nodes: beaten (violet, with the stars earned underneath), current (bright,
// pulsing cyan ring) and locked (dim, padlock, not clickable). Hovering or
// keyboard-focusing a node opens a glass tooltip with a thumbnail drawn by
// the real terrain and water shaders, the hint, best % and stars.
class LevelSelectScene : public Scene {
public:
    explicit LevelSelectScene(App& app);
    void update(float dt) override;
    void prepare() override;
    void drawWorld() override;
    void drawUi() override;

private:
    void layout();                                   // recompute on window resize
    Vector2 pointAt(float arc, Vector2* tangent = nullptr) const;
    float fillArc() const;                           // current water front (px of arc)
    int nodeUnder(Vector2 p) const;
    void open(int index);
    void moveFocus(int dir);
    void drawNode(int i, float appear);
    void drawTooltip(int i);

    // Tooltip size and text layout for level i.
    struct TipMetrics {
        float w, h, pad, thumbW, thumbH, capS, nameS, hintS, lineH, statsH;
        Vector2 capM, nameM;
        std::vector<std::string> hint;
    };
    TipMetrics tipMetrics(int i) const;

    App& app_;
    float layoutW_ = 0.0f, layoutH_ = 0.0f;
    std::vector<Vector2> pts_;   // the channel's centre line, finely sampled
    std::vector<float> arc_;     // arc length (px) at each sample
    std::vector<Vector2> nodes_; // node centres
    std::vector<float> nodeArc_; // arc length at each node
    float channelW_ = 0.0f, nodeR_ = 0.0f, headerBottom_ = 0.0f, safeTop_ = 0.0f;
    float tipBand_ = 0.0f;  // height reserved above the river for tooltips
    float riverTop_ = 0.0f; // top of the highest node

    int focus_ = 0;   // keyboard focus (always an unlocked level)
    int hover_ = -1;  // node under the mouse, locked ones included
    int tipFor_ = -1; // node the tooltip shows
    float tipAlpha_ = 0.0f;
    float time_ = 0.0f;

    // Water fill animation, in level indices: from where it was last shown to
    // the target (n = past the last node, into the end pool).
    int fillFrom_ = 0, fillTo_ = 0;

    std::vector<spillway::Vec2> waterPos_, waterVel_;
};
