#include "assets.h"

#include <rlgl.h>

#include <cmath>

namespace {

Assets gAssets;

std::string findGameDir() {
    // Assets are copied next to the executable at build time; fall back to
    // the source tree when running from somewhere else.
    const std::string exeDir = GetApplicationDirectory();
    if (DirectoryExists((exeDir + "shaders").c_str())) return exeDir;
    return std::string(SPILLWAY_SOURCE_GAME_DIR) + "/";
}

std::vector<int> glyphCodepoints() {
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c : {0x00B7, 0x2014, 0x2013, 0x2026, 0x00D7, 0x2190, 0x2192, 0x2191, 0x2193}) cps.push_back(c);
    return cps;
}

} // namespace

Assets& assets() { return gAssets; }

bool Assets::loadShader(const std::string& file, Shader& out, std::string& error) {
    const std::string path = gameDir_ + "shaders/" + file;
    if (!FileExists(path.c_str())) {
        error = "Missing shader file: " + path;
        return false;
    }
    out = LoadShader(nullptr, path.c_str());
    if (!IsShaderValid(out) || out.id == rlGetShaderIdDefault()) {
        error = "Shader failed to compile: " + path + " (see the log above for the GLSL error)";
        return false;
    }
    return true;
}

bool Assets::load(std::string& error) {
    gameDir_ = findGameDir();
    levelDir = gameDir_ + "levels/";

    // Fonts
    const char* files[] = {"Outfit-Regular.ttf", "Outfit-Medium.ttf", "Outfit-SemiBold.ttf", "JetBrainsMono-Regular.ttf"};
    const std::vector<int> sizes[] = {{24, 48, 96}, {24, 48, 96}, {32, 64, 128, 256}, {20, 40}};
    std::vector<int> cps = glyphCodepoints();
    for (int r = 0; r < static_cast<int>(FontRole::Count); ++r) {
        const std::string path = gameDir_ + "assets/fonts/" + files[r];
        if (!FileExists(path.c_str())) {
            error = "Missing font file: " + path;
            return false;
        }
        for (int px : sizes[r]) {
            Font f = LoadFontEx(path.c_str(), px, cps.data(), static_cast<int>(cps.size()));
            // Mipmaps + trilinear filtering keep downscaled glyphs smooth.
            GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            fonts_[r].push_back(f);
        }
    }

    // Shaders
    if (!loadShader("water.fs", water.shader, error)) return false;
    water.texel = GetShaderLocation(water.shader, "texel");
    water.threshold = GetShaderLocation(water.shader, "threshold");
    water.rimPx = GetShaderLocation(water.shader, "rimPx");
    water.dpi = GetShaderLocation(water.shader, "dpi");
    water.time = GetShaderLocation(water.shader, "time");
    water.slowColor = GetShaderLocation(water.shader, "slowColor");
    water.fastColor = GetShaderLocation(water.shader, "fastColor");

    if (!loadShader("blob.fs", blobSplat, error)) return false;
    if (!loadShader("glow.fs", glow.shader, error)) return false;
    glow.quadSize = GetShaderLocation(glow.shader, "quadSize");
    glow.halfSize = GetShaderLocation(glow.shader, "halfSize");
    glow.radius = GetShaderLocation(glow.shader, "radius");
    glow.softness = GetShaderLocation(glow.shader, "softness");
    glow.inside = GetShaderLocation(glow.shader, "inside");

    if (!loadShader("terrain.fs", terrain.shader, error)) return false;
    Shader& t = terrain.shader;
    auto loc = [&](const char* name) { return GetShaderLocation(t, name); };
    terrain.sdf = loc("sdf");
    terrain.sdfSize = loc("sdfSize");
    terrain.field = loc("field");
    terrain.fieldSize = loc("fieldSize");
    terrain.waterField = loc("waterField");
    terrain.dugTime = loc("dugTime");
    terrain.cellSize = loc("cellSize");
    terrain.levelSize = loc("levelSize");
    terrain.texel = loc("texel");
    terrain.renderHeight = loc("renderHeight");
    terrain.dpi = loc("dpi");
    terrain.viewOffset = loc("viewOffset");
    terrain.viewScale = loc("viewScale");
    terrain.time = loc("time");
    terrain.strataA = loc("strataA");
    terrain.strataB = loc("strataB");
    terrain.strataC = loc("strataC");
    terrain.rimColor = loc("rimColor");
    terrain.waterTint = loc("waterTint");
    terrain.rockBody = loc("rockBody");
    terrain.rockEdge = loc("rockEdge");
    terrain.rockEdgeAlt = loc("rockEdgeAlt");
    terrain.wallColor = loc("wallColor");

    if (!loadShader("bright.fs", bright, error)) return false;
    brightThreshold = GetShaderLocation(bright, "threshold");
    brightKnee = GetShaderLocation(bright, "knee");
    if (!loadShader("blur.fs", blur, error)) return false;
    blurDirection = GetShaderLocation(blur, "direction");
    if (!loadShader("composite.fs", composite, error)) return false;
    compositeBloom = GetShaderLocation(composite, "bloom");
    compositeStrength = GetShaderLocation(composite, "bloomStrength");
    compositeVignette = GetShaderLocation(composite, "vignette");
    compositeResolution = GetShaderLocation(composite, "resolution");

    // Soft blob: alpha = (1 - r^2)^3, the same shape as the poly6 kernel.
    const int n = 128;
    Image img = GenImageColor(n, n, BLANK);
    auto* px = static_cast<Color*>(img.data);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / n * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / n * 2.0f - 1.0f;
            const float q = std::max(0.0f, 1.0f - (dx * dx + dy * dy));
            px[y * n + x] = {255, 255, 255, static_cast<unsigned char>(std::lround(255.0f * q * q * q))};
        }
    }
    blob = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&blob);
    SetTextureFilter(blob, TEXTURE_FILTER_TRILINEAR);
    return true;
}

void Assets::unload() {
    for (auto& list : fonts_) {
        for (Font& f : list) UnloadFont(f);
        list.clear();
    }
    for (Shader* s : {&water.shader, &terrain.shader, &bright, &blur, &composite, &blobSplat, &glow.shader})
        if (s->id && s->id != rlGetShaderIdDefault()) UnloadShader(*s);
    if (blob.id) UnloadTexture(blob);
}

const Font& Assets::font(FontRole role, float physicalSize) const {
    const auto& list = fonts_[static_cast<int>(role)];
    for (const Font& f : list)
        if (static_cast<float>(f.baseSize) >= physicalSize * 0.95f) return f;
    return list.back();
}

namespace {
float gDpi = 1.0f;
}
// Set by the renderer from the real framebuffer each frame.
float uiDpi() { return gDpi; }
void setUiDpi(float dpi) { gDpi = dpi; }

Vector2 measureText(FontRole role, const std::string& text, float size, float tracking) {
    const Font& f = assets().font(role, size * uiDpi());
    return MeasureTextEx(f, text.c_str(), size, tracking * size);
}

void drawText(FontRole role, const std::string& text, Vector2 pos, float size, Color color, float tracking) {
    const Font& f = assets().font(role, size * uiDpi());
    DrawTextEx(f, text.c_str(), pos, size, tracking * size, color);
}
