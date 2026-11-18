#pragma once

// Frame pipeline. Everything draws into offscreen targets at the physical
// (HiDPI) resolution, while game code works in logical pixels.
//
//   world layer (sceneRT): background, drains, water, terrain, effects
//        |  bright pass -> blur H/V x2 at half res = bloom
//        v
//   composite (frameRT): scene + bloom + vignette + dither
//        |  optional: quarter-res blur of the frame = glass backdrop
//        v
//   UI layer (frameRT): HUD, cards, menus (crisp, no bloom)
//        v
//   present: frameRT to the screen, cross-faded with a snapshot of the
//            previous scene during scene transitions.

#include <raylib.h>

#include <span>

#include "spillway/vec2.h"
#include "view.h"

// Water look: kernel radius in metres (~2.5x the 0.135 m particle spacing)
// and the field value treated as the surface. A full interior sums to ~5.
inline constexpr float kWaterKernelRadius = 0.34f;
inline constexpr float kWaterThreshold = 1.35f;

class Renderer {
public:
    void init();
    void unload();
    // Recreate targets if the window or DPI changed. Call once per frame.
    void ensureSize();

    float dpi() const { return dpi_; }
    int renderWidth() const { return rw_; }
    int renderHeight() const { return rh_; }

    void beginWorld();
    void endWorld(bool wantBackdrop);
    void beginUi();
    void endUi();
    void present(float previousSceneAlpha);
    void captureSnapshot(); // freeze the current frame for a cross-fade

    // Draws the blurred world (from endWorld(true)) over the whole screen.
    void drawBackdrop(float alpha);

    // Metaball water. Call between beginWorld/endWorld.
    void drawWater(std::span<const spillway::Vec2> positions, std::span<const spillway::Vec2> velocities,
                   const View& view);

    // Normal alpha blending that keeps the destination alpha at 1 (raylib's
    // default would make translucent UI punch holes in an opaque target).
    static void blendNormal();
    static void blendAdditive();

    Image screenshot() const;
    // The water field drawn by the last drawWater() call (for water-lit terrain).
    const Texture2D& waterField() const { return blob_.texture; }

private:
    void bindLayer(RenderTexture2D& target, bool logical);
    void unbindLayer(bool logical);
    void blit(const Texture2D& src, RenderTexture2D& dst, Shader* shader);
    void blurPingPong(RenderTexture2D& a, RenderTexture2D& b, int iterations);
    void freeTargets();

    int rw_ = 0, rh_ = 0;
    float dpi_ = 1.0f;
    RenderTexture2D scene_{}, frame_{}, snapshot_{}, blob_{};
    RenderTexture2D bloomA_{}, bloomB_{};
    RenderTexture2D backA_{}, backB_{};
    bool haveBackdrop_ = false;
};
