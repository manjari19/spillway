#include "level_select_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "input.h"
#include "theme.h"
#include "widgets.h"

using namespace spillway;

namespace {

// A small picture of a level: one pixel per terrain cell.
Texture2D makeThumbnail(const LevelDesc& level) {
    const Terrain t = buildTerrain(level);
    Image img = GenImageColor(t.width(), t.height(), Color{19, 17, 42, 255});
    auto* px = static_cast<Color*>(img.data);
    for (int y = 0; y < t.height(); ++y) {
        const float depth = static_cast<float>(y) / static_cast<float>(t.height());
        for (int x = 0; x < t.width(); ++x) {
            const Vec2 c = t.cellCentre(x, y);
            Color col{19, 17, 42, 255};
            switch (t.at(x, y)) {
            case Cell::Dirt: col = ease::mix(theme::strataA, theme::strataB, 0.5f + 0.5f * std::sin(c.y * 7.8f)); col = ease::mix(col, Color{20, 16, 40, 255}, depth * 0.35f); break;
            case Cell::Rock: col = Color{44, 40, 92, 255}; break;
            case Cell::Wall: col = theme::wall; break;
            case Cell::Empty:
                for (const Rect& w : level.water) if (w.contains(c)) col = theme::waterSlow;
                break;
            }
            for (const Rect& d : level.drains) if (d.contains(c)) col = theme::cyan;
            for (const Rect& p : level.pits) if (p.contains(c)) col = theme::danger;
            px[y * t.width() + x] = col;
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    return tex;
}

void padlock(Vector2 c, float size, Color color) {
    const float bw = size, bh = size * 0.78f;
    const Rectangle body{c.x - bw * 0.5f, c.y - bh * 0.2f, bw, bh};
    DrawRing({c.x, c.y - bh * 0.2f}, size * 0.26f, size * 0.36f, 180.0f, 360.0f, 24, color);
    DrawRectangleRounded(body, 0.3f, 8, color);
    DrawCircleV({c.x, body.y + bh * 0.45f}, size * 0.09f, Color{20, 18, 40, 255});
}

std::string fmt(const char* f, int v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

} // namespace

LevelSelectScene::LevelSelectScene(App& app) : app_(app) {
    for (int i = 0; i < app_.levelCount(); ++i) thumbs_.push_back(makeThumbnail(app_.level(i)));
    // Start on the first level that still needs beating.
    focus_ = app_.progress().firstUnbeaten(app_.levelCount());
}

LevelSelectScene::~LevelSelectScene() {
    for (Texture2D& t : thumbs_) UnloadTexture(t);
}

Rectangle LevelSelectScene::cardRect(int index) const {
    const float s = ui::scale();
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const int cols = 3;
    const int rows = (app_.levelCount() + cols - 1) / cols;
    const float gap = 24.0f * s;
    const float pad = 16.0f * s;
    const float textH = 14.0f * s + 26.0f * s + 10.0f * s + 22.0f * s; // name row + stars row
    // Fit the width, and the height below the header (with room for the
    // hover scale-up), whichever is tighter.
    const float top0 = 130.0f * s, bottomMargin = 36.0f * s;
    const float maxCh = (sh - top0 - bottomMargin - gap * (rows - 1)) / rows;
    const float cwFromH = (maxCh - textH - 2 * pad) * 16.0f / 9.0f + 2 * pad;
    const float cw = std::min({330.0f * s, (sw - 2 * 64.0f * s - gap * (cols - 1)) / cols, cwFromH});
    const float thumbH = (cw - 2 * pad) * 9.0f / 16.0f;
    const float ch = pad + thumbH + textH + pad;
    const float gridH = rows * ch + (rows - 1) * gap;
    const float top = std::max(top0, top0 + (sh - top0 - bottomMargin - gridH) * 0.5f);
    const int row = index / cols, col = index % cols;
    const int inRow = std::min(cols, app_.levelCount() - row * cols);
    const float rowW = inRow * cw + (inRow - 1) * gap;
    return {(sw - rowW) * 0.5f + col * (cw + gap), top + row * (ch + gap), cw, ch};
}

void LevelSelectScene::open(int index) {
    if (app_.progress().unlocked(index)) app_.startLevel(index);
}

void LevelSelectScene::update(float dt) {
    time_ += dt;
    const int n = app_.levelCount();
    if (input::pressed(KEY_ESCAPE)) app_.openMenu();
    if (input::pressed(KEY_RIGHT)) focus_ = std::min(n - 1, focus_ + 1);
    if (input::pressed(KEY_LEFT)) focus_ = std::max(0, focus_ - 1);
    if (input::pressed(KEY_DOWN)) focus_ = std::min(n - 1, focus_ + 3);
    if (input::pressed(KEY_UP)) focus_ = std::max(0, focus_ - 3);
    for (int i = 0; i < n; ++i) {
        if (CheckCollisionPointRec(input::mouse(), cardRect(i))) {
            focus_ = i;
            if (input::mousePressed(MOUSE_BUTTON_LEFT)) open(i);
        }
    }
    if (input::pressed(KEY_ENTER) || input::pressed(KEY_SPACE)) open(focus_);
}

void LevelSelectScene::drawWorld() {
    // A quiet backdrop: just the background gradient (drawn by the renderer)
    // and a soft violet glow low in the screen.
    const Texture2D& blob = assets().blob;
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                   {sw * 0.1f, sh * 0.55f, sw * 0.8f, sh * 0.9f}, {0, 0}, 0.0f, ease::alpha(theme::violet, 0.10f));
}

void LevelSelectScene::drawUi() {
    const float s = ui::scale();
    const float sw = static_cast<float>(GetScreenWidth());
    const Progress& pr = app_.progress();
    const float in = ease::outCubic(std::min(1.0f, time_ / 0.5f));

    // Header: title on the left, back hint and total stars.
    const float x = 64.0f * s, y = 48.0f * s;
    drawText(FontRole::SemiBold, "Levels", {x, y}, theme::type::title * 1.2f * s, ease::alpha(theme::text, in));
    const float titleH = measureText(FontRole::SemiBold, "Levels", theme::type::title * 1.2f * s).y;
    drawText(FontRole::Regular, "Esc to go back", {x + 2.0f * s, y + titleH + 2.0f * s}, theme::type::small * s,
             ease::alpha(theme::textDim, in));
    const std::string total = std::to_string(pr.totalStars()) + " / " + std::to_string(3 * app_.levelCount());
    const float ts = theme::type::body * s;
    const Vector2 tm = measureText(FontRole::SemiBold, total, ts);
    const float starR = 10.0f * s;
    const float tx = sw - x - tm.x;
    drawText(FontRole::SemiBold, total, {tx, y + 14.0f * s}, ts, ease::alpha(theme::text, in));
    ui::star({tx - starR - 10.0f * s, y + 14.0f * s + tm.y * 0.5f}, starR, 1.0f, in);

    for (int i = 0; i < app_.levelCount(); ++i) {
        const Rectangle base = cardRect(i);
        const bool unlocked = pr.unlocked(i);
        const bool hot = i == focus_;
        const float h = ui::animate("lvl:" + std::to_string(i), hot ? 1.0f : 0.0f, 12.0f);
        // Cards rise in one after another.
        const float appear = ease::outCubic(std::clamp((time_ - 0.05f * static_cast<float>(i)) / 0.45f, 0.0f, 1.0f));
        const float k = 1.0f + 0.03f * h * (unlocked ? 1.0f : 0.3f);
        const Rectangle r{base.x + base.width * (1 - k) * 0.5f, base.y + base.height * (1 - k) * 0.5f + (1 - appear) * 14.0f * s,
                          base.width * k, base.height * k};
        const float a = appear * (unlocked ? 1.0f : 0.6f);
        if (h > 0.01f && unlocked) ui::glow(r, 22.0f * s, 18.0f * s, ease::alpha(theme::violet, 0.45f * h * appear), 0.0f);
        ui::glass(r, 22.0f * s, appear);
        if (hot) DrawRectangleRoundedLinesEx(r, ui::roundness(r, 22.0f * s), 24, 1.5f, ease::alpha(Color{180, 160, 255, 120}, h * appear));

        const float pad = 16.0f * s * k;
        const Rectangle thumb{r.x + pad, r.y + pad, r.width - 2 * pad, (r.width - 2 * pad) * 9.0f / 16.0f};
        DrawTexturePro(thumbs_[static_cast<size_t>(i)], {0, 0, static_cast<float>(thumbs_[static_cast<size_t>(i)].width),
                                                        static_cast<float>(thumbs_[static_cast<size_t>(i)].height)},
                       thumb, {0, 0}, 0.0f, ease::alpha(WHITE, a));
        DrawRectangleLinesEx(thumb, 1.0f, ease::alpha(Color{255, 255, 255, 30}, appear));
        if (!unlocked) {
            DrawRectangleRec(thumb, ease::alpha(Color{8, 6, 20, 255}, 0.55f * appear));
            padlock({thumb.x + thumb.width * 0.5f, thumb.y + thumb.height * 0.48f}, 30.0f * s, ease::alpha(theme::textDim, appear));
        }

        float ty = thumb.y + thumb.height + 14.0f * s;
        const std::string num = fmt("%02d", i + 1);
        const float ns = theme::type::body * s;
        drawText(FontRole::SemiBold, num, {thumb.x, ty}, ns, ease::alpha(unlocked ? theme::cyan : theme::textMuted, appear));
        const float numW = measureText(FontRole::SemiBold, num, ns).x;
        drawText(FontRole::Medium, app_.level(i).name, {thumb.x + numW + 10.0f * s, ty}, ns,
                 ease::alpha(unlocked ? theme::text : theme::textMuted, appear));
        ty += 26.0f * s + 10.0f * s;

        if (unlocked) {
            const LevelRecord rec = i < static_cast<int>(pr.levels.size()) ? pr.levels[static_cast<size_t>(i)] : LevelRecord{};
            const float sr = 9.0f * s;
            for (int st = 0; st < 3; ++st)
                ui::star({thumb.x + sr + static_cast<float>(st) * (sr * 2.0f + 6.0f * s), ty + sr}, sr, st < rec.stars ? 1.0f : 0.0f, appear);
            const std::string best = rec.stars > 0 ? fmt("Best %d%%", rec.bestPercent) : std::string("Not beaten yet");
            const float bs = theme::type::small * s;
            const Vector2 bm = measureText(FontRole::Regular, best, bs);
            drawText(FontRole::Regular, best, {thumb.x + thumb.width - bm.x, ty + sr - bm.y * 0.5f}, bs, ease::alpha(theme::textDim, appear));
        } else {
            const std::string why = fmt("Beat level %d to unlock", i);
            drawText(FontRole::Regular, why, {thumb.x, ty}, theme::type::small * s, ease::alpha(theme::textMuted, appear));
        }
    }
}
