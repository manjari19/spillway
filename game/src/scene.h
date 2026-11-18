#pragma once

// A screen of the game. The App calls, every frame:
//   update(dt) -> prepare() -> drawWorld() [bloomed] -> drawUi() [crisp]
class Scene {
public:
    virtual ~Scene() = default;
    virtual void update(float dt) = 0;
    virtual void prepare() {}       // draw into private targets before the world layer
    virtual void drawWorld() = 0;   // logical px, goes through bloom
    virtual void drawUi() = 0;      // logical px, on top, no bloom
    virtual bool wantsBackdrop() const { return false; } // blurred world for glass cards
};
