#pragma once

#include <memory>
#include <string>

#include "renderer.h"
#include "scene.h"
#include "spillway/level.h"

// Owns the window loop, renderer and current scene. Scene changes are
// requested with goTo() and happen at the end of the frame with a cross-fade.
class App {
public:
    int run();

    void goTo(std::unique_ptr<Scene> next) { pending_ = std::move(next); }
    void quit() { quit_ = true; }
    Renderer& renderer() { return renderer_; }

    std::unique_ptr<Scene> makeMenu();
    std::unique_ptr<Scene> makeLevel(int index); // 0-based; menu on failure
    bool levelExists(int index) const;
    std::string levelPath(int index) const;

private:
    Renderer renderer_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pending_;
    bool quit_ = false;
    float fade_ = 1.0f; // 1 = showing only the previous frame
};
