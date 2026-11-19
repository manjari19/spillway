#include "renderer.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>

#include "assets.h"
#include "theme.h"

extern "C" {
struct GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height);
}

namespace {

// The real framebuffer size in physical pixels. raylib caches this and the
// cache can go stale when macOS adjusts the window at creation, so ask GLFW.
void framebufferSize(int& w, int& h) {
    w = GetRenderWidth();
    h = GetRenderHeight();
    if (GLFWwindow* win = glfwGetCurrentContext()) {
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(win, &fw, &fh);
        if (fw > 0 && fh > 0) { w = fw; h = fh; }
    }
}

// Render target with a 16-bit float colour buffer. The water field is a sum
// of many overlapping blobs, which would saturate an 8-bit target.
RenderTexture2D loadFloatTarget(int w, int h) {
    RenderTexture2D t{};
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer(t.id);
    t.texture.id = rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    t.texture.width = w;
    t.texture.height = h;
    t.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    t.texture.mipmaps = 1;
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferComplete(t.id);
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    return t;
}

RenderTexture2D loadTarget(int w, int h) {
    RenderTexture2D t = LoadRenderTexture(std::max(1, w), std::max(1, h));
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
    return t;
}

void unloadTarget(RenderTexture2D& t) {
    if (t.id) UnloadRenderTexture(t);
    t = {};
}

Rectangle flipped(const Texture2D& t) {
    return {0, 0, static_cast<float>(t.width), -static_cast<float>(t.height)};
}

void setVec3(Shader s, int loc, Color c) {
    const float v[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
    SetShaderValue(s, loc, v, SHADER_UNIFORM_VEC3);
}

} // namespace

void Renderer::blendNormal() {
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD,
                              RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}

void Renderer::blendAdditive() { BeginBlendMode(BLEND_ADDITIVE); }

void Renderer::init() { ensureSize(); }

void Renderer::freeTargets() {
    for (RenderTexture2D* t : {&scene_, &frame_, &snapshot_, &blob_, &bloomA_, &bloomB_, &backA_, &backB_})
        unloadTarget(*t);
}

void Renderer::unload() { freeTargets(); }

void Renderer::ensureSize() {
    int rw = 1, rh = 1;
    framebufferSize(rw, rh);
    rw = std::max(1, rw);
    rh = std::max(1, rh);
    dpi_ = static_cast<float>(rw) / static_cast<float>(std::max(1, GetScreenWidth()));
    setUiDpi(dpi_);
    if (rw == rw_ && rh == rh_ && scene_.id) return;
    freeTargets();
    rw_ = rw;
    rh_ = rh;
    scene_ = loadTarget(rw, rh);
    frame_ = loadTarget(rw, rh);
    snapshot_ = loadTarget(rw, rh);
    blob_ = loadFloatTarget(rw, rh);
    bloomA_ = loadTarget(rw / 2, rh / 2);
    bloomB_ = loadTarget(rw / 2, rh / 2);
    backA_ = loadTarget(rw / 4, rh / 4);
    backB_ = loadTarget(rw / 4, rh / 4);
    // Start from black so the first scene fades in.
    BeginTextureMode(snapshot_);
    ClearBackground(BLACK);
    EndTextureMode();
}

void Renderer::bindLayer(RenderTexture2D& target, bool logical) {
    BeginTextureMode(target);
    if (logical) {
        // Draw in logical pixels; the target is in physical pixels.
        rlPushMatrix();
        rlScalef(dpi_, dpi_, 1.0f);
    }
    blendNormal();
}

void Renderer::unbindLayer(bool logical) {
    EndBlendMode();
    if (logical) rlPopMatrix();
    EndTextureMode();
}

void Renderer::beginWorld() {
    bindLayer(scene_, true);
    DrawRectangleGradientV(0, 0, static_cast<int>(std::ceil(logicalWidth())), static_cast<int>(std::ceil(logicalHeight())),
                           theme::bgTop, theme::bgBottom);
}

void Renderer::blit(const Texture2D& src, RenderTexture2D& dst, Shader* shader) {
    BeginTextureMode(dst);
    ClearBackground(BLANK);
    if (shader) BeginShaderMode(*shader);
    DrawTexturePro(src, flipped(src), {0, 0, static_cast<float>(dst.texture.width), static_cast<float>(dst.texture.height)},
                   {0, 0}, 0.0f, WHITE);
    if (shader) EndShaderMode();
    EndTextureMode();
}

void Renderer::blurPingPong(RenderTexture2D& a, RenderTexture2D& b, int iterations) {
    Assets& as = assets();
    for (int i = 0; i < iterations; ++i) {
        const float spread = 1.0f + static_cast<float>(i);
        const float h[2] = {spread / static_cast<float>(a.texture.width), 0.0f};
        const float v[2] = {0.0f, spread / static_cast<float>(a.texture.height)};
        SetShaderValue(as.blur, as.blurDirection, h, SHADER_UNIFORM_VEC2);
        blit(a.texture, b, &as.blur);
        SetShaderValue(as.blur, as.blurDirection, v, SHADER_UNIFORM_VEC2);
        blit(b.texture, a, &as.blur);
    }
}

void Renderer::endWorld(bool wantBackdrop) {
    unbindLayer(true);
    Assets& as = assets();

    // Bloom: bright pass at half resolution, then blur.
    const float threshold = 0.42f, knee = 0.25f;
    SetShaderValue(as.bright, as.brightThreshold, &threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(as.bright, as.brightKnee, &knee, SHADER_UNIFORM_FLOAT);
    if (bloom_) {
        blit(scene_.texture, bloomA_, &as.bright);
        blurPingPong(bloomA_, bloomB_, 3);
    }

    // Composite into the frame.
    BeginTextureMode(frame_);
    BeginShaderMode(as.composite);
    const float strength = bloom_ ? 0.8f : 0.0f, vignette = 0.28f;
    const float res[2] = {static_cast<float>(rw_), static_cast<float>(rh_)};
    SetShaderValue(as.composite, as.compositeStrength, &strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(as.composite, as.compositeVignette, &vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(as.composite, as.compositeResolution, res, SHADER_UNIFORM_VEC2);
    SetShaderValueTexture(as.composite, as.compositeBloom, bloomA_.texture);
    DrawTexturePro(scene_.texture, flipped(scene_.texture), {0, 0, res[0], res[1]}, {0, 0}, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();

    haveBackdrop_ = wantBackdrop;
    if (wantBackdrop) {
        blit(frame_.texture, backA_, nullptr);
        blurPingPong(backA_, backB_, 3);
    }
}

void Renderer::drawBackdrop(float alpha) {
    if (!haveBackdrop_) return;
    DrawTexturePro(backA_.texture, flipped(backA_.texture),
                   {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, {0, 0}, 0.0f,
                   Color{255, 255, 255, static_cast<unsigned char>(std::lround(255.0f * std::clamp(alpha, 0.0f, 1.0f)))});
}

void Renderer::beginUi() { bindLayer(frame_, true); }
void Renderer::endUi() { unbindLayer(true); }

void Renderer::captureSnapshot() { blit(frame_.texture, snapshot_, nullptr); }

void Renderer::present(float previousSceneAlpha) {
    BeginDrawing();
    // Present in physical pixels with our own viewport, matching the real
    // framebuffer exactly (see framebufferSize).
    rlDrawRenderBatchActive();
    rlViewport(0, 0, rw_, rh_);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, rw_, rh_, 0, 0.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    // Anything not covered by the frame gets the background colour, never a
    // hard black band.
    ClearBackground(theme::bgTop);
    const Rectangle dst{0, 0, static_cast<float>(rw_), static_cast<float>(rh_)};
    // The frame is opaque: copy it without blending.
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(frame_.texture, flipped(frame_.texture), dst, {0, 0}, 0.0f, WHITE);
    EndBlendMode();
    if (previousSceneAlpha > 0.0f) {
        const auto a = static_cast<unsigned char>(std::lround(255.0f * std::clamp(previousSceneAlpha, 0.0f, 1.0f)));
        DrawTexturePro(snapshot_.texture, flipped(snapshot_.texture), dst, {0, 0}, 0.0f, Color{255, 255, 255, a});
    }
    EndDrawing();
}

void Renderer::drawWater(std::span<const spillway::Vec2> pos, std::span<const spillway::Vec2> vel, const View& view,
                         float densityScale) {
    Assets& as = assets();
    // 1. Splat one smooth kernel per particle into the float field target.
    //    The kernel radius is ~2.5x the particle spacing (0.135 m), so every
    //    pixel inside the water sums many overlapping kernels and the field
    //    is smooth: no visible individual particles.
    unbindLayer(true);
    bindLayer(blob_, true);
    ClearBackground(BLANK);
    BeginBlendMode(BLEND_ADD_COLORS);
    BeginShaderMode(as.blobSplat);
    const float radius = kWaterKernelRadius * view.scale; // logical px
    const Rectangle src{0, 0, static_cast<float>(as.blob.width), static_cast<float>(as.blob.height)};
    for (size_t i = 0; i < pos.size(); ++i) {
        const Vector2 c = view.toScreen(pos[i]);
        const float speed = std::clamp(spillway::length(vel[i]) / 5.0f, 0.0f, 1.0f);
        const Color tint{255, static_cast<unsigned char>(std::lround(speed * 255.0f)), 0, 255};
        DrawTexturePro(as.blob, src, {c.x - radius, c.y - radius, 2 * radius, 2 * radius}, {0, 0}, 0.0f, tint);
    }
    EndShaderMode();
    EndBlendMode();
    unbindLayer(true);

    // 2. Threshold the field into one surface, composited over the scene.
    bindLayer(scene_, true);
    WaterShader& ws = as.water;
    const float texel[2] = {1.0f / static_cast<float>(rw_), 1.0f / static_cast<float>(rh_)};
    const float threshold = kWaterThreshold * densityScale, rim = 1.6f * dpi_, t = static_cast<float>(GetTime());
    SetShaderValue(ws.shader, ws.texel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(ws.shader, ws.threshold, &threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ws.shader, ws.rimPx, &rim, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ws.shader, ws.dpi, &dpi_, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ws.shader, ws.time, &t, SHADER_UNIFORM_FLOAT);
    setVec3(ws.shader, ws.slowColor, theme::waterSlow);
    setVec3(ws.shader, ws.fastColor, theme::waterFast);
    BeginShaderMode(ws.shader);
    DrawTexturePro(blob_.texture, flipped(blob_.texture), {0, 0, logicalWidth(), logicalHeight()}, {0, 0}, 0.0f, WHITE);
    EndShaderMode();
    // Leave the scene layer bound, as the caller expects.
}

void Renderer::renderOffscreen(RenderTexture2D& dst, const std::function<void()>& prepare,
                               const std::function<void()>& drawWorld) {
    // Swap in targets of the thumbnail's size, run the normal world
    // pipeline with `dst` as the frame, then put everything back.
    const int savedW = rw_, savedH = rh_;
    const float savedDpi = dpi_;
    const RenderTexture2D savedScene = scene_, savedFrame = frame_, savedBlob = blob_, savedA = bloomA_, savedB = bloomB_;
    const bool savedBackdrop = haveBackdrop_;

    rw_ = dst.texture.width;
    rh_ = dst.texture.height;
    dpi_ = 1.0f;
    scene_ = loadTarget(rw_, rh_);
    blob_ = loadFloatTarget(rw_, rh_);
    bloomA_ = loadTarget(rw_ / 2, rh_ / 2);
    bloomB_ = loadTarget(rw_ / 2, rh_ / 2);
    frame_ = dst;

    prepare();
    beginWorld();
    drawWorld();
    endWorld(false);

    for (RenderTexture2D* t : {&scene_, &blob_, &bloomA_, &bloomB_}) unloadTarget(*t);
    rw_ = savedW;
    rh_ = savedH;
    dpi_ = savedDpi;
    scene_ = savedScene;
    frame_ = savedFrame;
    blob_ = savedBlob;
    bloomA_ = savedA;
    bloomB_ = savedB;
    haveBackdrop_ = savedBackdrop;
}

Image Renderer::screenshot() const {
    Image img = LoadImageFromTexture(frame_.texture);
    ImageFlipVertical(&img);
    return img;
}
