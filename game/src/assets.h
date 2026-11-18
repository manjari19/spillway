#pragma once

// Fonts, shaders and generated textures, loaded once at startup.

#include <raylib.h>

#include <string>
#include <vector>

enum class FontRole { Regular = 0, Medium, SemiBold, Mono, Count };

struct WaterShader {
    Shader shader{};
    int texel, threshold, rimPx, dpi, time, slowColor, fastColor;
};
struct GlowShader {
    Shader shader{};
    int quadSize, halfSize, radius, softness, inside;
};
struct TerrainShader {
    Shader shader{};
    int sdf, sdfSize, field, fieldSize, waterField, dugTime, cellSize, levelSize, texel, renderHeight, dpi, viewOffset,
        viewScale, time;
    int strataA, strataB, strataC, rimColor, waterTint, rockBody, rockEdge, rockEdgeAlt, wallColor;
};

class Assets {
public:
    // Loads everything. Returns false and fills `error` with a readable
    // message (e.g. which shader file is missing) on failure.
    bool load(std::string& error);
    void unload();

    // Font atlas closest to (and not smaller than) the physical pixel size.
    const Font& font(FontRole role, float physicalSize) const;

    WaterShader water;
    GlowShader glow;
    Shader blobSplat{};
    TerrainShader terrain;
    Shader bright{}, blur{}, composite{};
    int brightThreshold = -1, brightKnee = -1, blurDirection = -1;
    int compositeBloom = -1, compositeStrength = -1, compositeVignette = -1, compositeResolution = -1;

    Texture2D blob{}; // soft radial falloff, white with alpha (1 - r^2)^3

    std::string levelDir;

private:
    bool loadShader(const std::string& file, Shader& out, std::string& error);

    std::string gameDir_;
    // Each role is loaded at a few pixel sizes; small text uses a small atlas
    // and large text a large one, so glyphs are never magnified.
    std::vector<Font> fonts_[static_cast<int>(FontRole::Count)];
};

Assets& assets();

// --- text, drawn in logical coordinates ---
float uiDpi(); // physical pixels per logical pixel
void setUiDpi(float dpi);
Vector2 measureText(FontRole role, const std::string& text, float size, float tracking = 0.0f);
void drawText(FontRole role, const std::string& text, Vector2 pos, float size, Color color, float tracking = 0.0f);
