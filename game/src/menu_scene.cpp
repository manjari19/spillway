#include "menu_scene.h"

#include <cmath>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "renderer.h"
#include "theme.h"
#include "widgets.h"

using namespace spillway;

namespace {
const char* kTitle = "SPILLWAY";
constexpr float kTitleTracking = 0.06f;
} // namespace

MenuScene::MenuScene(App& app) : app_(app) {
    auto level = loadLevelFile(assets().levelDir + "menu.txt", /*requireGoals=*/false);
    LevelDesc desc;
    if (level) {
        desc = *level.level;
    } else {
        // Keep the menu usable even if the backdrop file is missing.
        desc.size = {16, 9};
        desc.water.push_back({{4, 3}, {12, 7}});
    }
    world_ = std::make_unique<World>(desc);
    terrainView_.reset(world_->terrain());
    items_ = {{"Play", true}, {"Levels", false}, {"Settings", false}, {"Quit", true}};
    lastMouse_ = GetMousePosition();
}

float MenuScene::itemY(int index) const {
    const float s = ui::scale();
    const float titleH = measureText(FontRole::SemiBold, kTitle, theme::type::display * s, kTitleTracking).y;
    const float top = static_cast<float>(GetScreenHeight()) * 0.2f + titleH + 92.0f * s;
    return top + static_cast<float>(index) * 58.0f * s;
}

void MenuScene::activate(int index) {
    if (!items_[static_cast<size_t>(index)].enabled) return;
    const std::string& l = items_[static_cast<size_t>(index)].label;
    if (l == "Play") app_.goTo(app_.makeLevel(0));
    if (l == "Quit") app_.quit();
}

void MenuScene::update(float dt) {
    time_ += dt;
    intro_ = std::min(1.0f, intro_ + dt / 0.9f);
    const float s = ui::scale();
    const Vec2 extent = world_->terrain().extent();
    view_ = View::cover(extent, {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});

    // Gravity sways a little so the water keeps sloshing on its own.
    world_->fluid().setGravity({2.4f * std::sin(time_ * 0.42f), 12.0f});

    // Moving the mouse pushes water gently away from the cursor.
    const Vector2 m = GetMousePosition();
    const float moved = std::hypot(m.x - lastMouse_.x, m.y - lastMouse_.y);
    lastMouse_ = m;
    if (moved > 1.0f) stir_ = 0.25f;
    stir_ = std::max(0.0f, stir_ - dt);
    Interaction in;
    in.point = view_.toWorld(m);
    in.radius = 1.1f;
    in.strength = -14.0f;
    in.active = stir_ > 0.0f;
    world_->fluid().setInteraction(in);
    world_->update(dt);
    world_->events().clear();

    // Menu navigation: mouse hover or up/down, enter/click to activate.
    const float textX = 0.075f * static_cast<float>(GetScreenWidth());
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const Rectangle r{textX - 24.0f * s, itemY(i) - 6.0f * s, 340.0f * s, 52.0f * s};
        if (items_[static_cast<size_t>(i)].enabled && CheckCollisionPointRec(m, r)) hovered = i;
    }
    if (hovered >= 0 && moved > 0.0f) focus_ = hovered;
    auto step = [&](int dir) {
        int f = focus_;
        for (size_t k = 0; k < items_.size(); ++k) {
            f = (f + dir + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
            if (items_[static_cast<size_t>(f)].enabled) break;
        }
        focus_ = f;
    };
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) step(1);
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) step(-1);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) activate(focus_);
    if (hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) activate(hovered);

    // The highlight bar slides toward the focused item (ease-out approach).
    const float target = itemY(focus_);
    barY_ = barY_ < 0.0f ? target : ease::approach(barY_, target, 16.0f, dt);
    barAlpha_ = ease::approach(barAlpha_, 1.0f, 6.0f, dt);
}

void MenuScene::prepare() {
    terrainView_.sync(world_->terrain());
    terrainView_.prepare(app_.renderer(), view_);
}

void MenuScene::drawTitle(float alpha) const {
    const float s = ui::scale();
    const float x = 0.075f * static_cast<float>(GetScreenWidth());
    const float y = static_cast<float>(GetScreenHeight()) * 0.2f;
    drawText(FontRole::SemiBold, kTitle, {x, y}, theme::type::display * s, ease::alpha(theme::text, alpha), kTitleTracking);
}

void MenuScene::drawWorld() {
    app_.renderer().drawWater(world_->fluid().positions(), world_->fluid().velocities(), view_);
    terrainView_.draw(app_.renderer(), view_, world_->terrain());

    // Left scrim so the menu reads over the water, then the title drawn here
    // too so the bloom gives it a soft glow (the crisp copy is in drawUi).
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    // Solid behind the text column, then a long soft fade to the right.
    const int solid = static_cast<int>(static_cast<float>(sw) * 0.34f);
    DrawRectangle(0, 0, solid, sh, Color{5, 4, 13, 200});
    DrawRectangleGradientH(solid, 0, static_cast<int>(static_cast<float>(sw) * 0.36f), sh, Color{5, 4, 13, 200},
                           Color{5, 4, 13, 0});
    drawTitle(0.55f * ease::outCubic(intro_));
}

void MenuScene::drawUi() {
    const float s = ui::scale();
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float x = 0.075f * sw;
    const float in = ease::outCubic(intro_);

    drawTitle(in);
    const float titleY = sh * 0.2f;
    const float titleH = measureText(FontRole::SemiBold, kTitle, theme::type::display * s, kTitleTracking).y;
    drawText(FontRole::Regular, "A fluid puzzle about guiding water", {x + 4.0f * s, titleY + titleH + 4.0f * s},
             20.0f * s, ease::alpha(ease::mix(theme::textDim, theme::text, 0.35f), in));

    // Sliding highlight bar: a violet wash fading to the right, with a thin
    // glowing cyan edge on the left.
    const float barH = 50.0f * s;
    const Rectangle bar{x - 26.0f * s, barY_ - 8.0f * s, 360.0f * s, barH};
    const float ba = barAlpha_ * in;
    DrawRectangleGradientH(static_cast<int>(bar.x), static_cast<int>(bar.y), static_cast<int>(bar.width),
                           static_cast<int>(bar.height), ease::alpha(Color{109, 60, 255, 120}, ba),
                           ease::alpha(Color{109, 60, 255, 0}, ba));
    DrawRectangleRounded({bar.x, bar.y + 6.0f * s, 3.0f * s, bar.height - 12.0f * s}, 1.0f, 6,
                         ease::alpha(theme::cyan, ba));
    DrawRectangleRounded({bar.x - 3.0f * s, bar.y + 2.0f * s, 9.0f * s, bar.height - 4.0f * s}, 1.0f, 6,
                         ease::alpha(theme::cyan, ba * 0.18f));

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        const bool focused = i == focus_;
        const float h = ui::animate("menu:" + it.label, focused ? 1.0f : 0.0f, 12.0f);
        // Items slide in one after another on entry.
        const float appear = ease::outCubic(std::clamp((intro_ - 0.15f - 0.08f * static_cast<float>(i)) / 0.5f, 0.0f, 1.0f));
        const float ix = x + 10.0f * s * h - 18.0f * s * (1.0f - appear);
        const float y = itemY(i);
        Color c = it.enabled ? ease::mix(theme::textDim, theme::text, h) : theme::textMuted;
        drawText(FontRole::Medium, it.label, {ix, y}, theme::type::heading * s, ease::alpha(c, appear));
        if (!it.enabled) {
            const Vector2 lm = measureText(FontRole::Medium, it.label, theme::type::heading * s);
            const std::string tag = "SOON";
            const Vector2 tm = measureText(FontRole::Medium, tag, theme::type::caption * s, 0.14f);
            const Rectangle pill{ix + lm.x + 12.0f * s, y + (lm.y - tm.y) * 0.5f - 3.0f * s, tm.x + 14.0f * s, tm.y + 6.0f * s};
            DrawRectangleRounded(pill, 1.0f, 12, ease::alpha(Color{255, 255, 255, 14}, appear));
            drawText(FontRole::Medium, tag, {pill.x + 7.0f * s, pill.y + 3.0f * s}, theme::type::caption * s,
                     ease::alpha(theme::textMuted, appear), 0.14f);
        }
    }

    // Footer.
    const float fs = theme::type::small * s;
    drawText(FontRole::Regular, "Move the mouse to stir the water", {x, sh - 40.0f * s}, fs, ease::alpha(theme::textDim, in * 0.8f));
    const std::string right = "Up/Down + Enter, or click";
    const Vector2 rm = measureText(FontRole::Regular, right, fs);
    drawText(FontRole::Regular, right, {sw - x - rm.x, sh - 40.0f * s}, fs, ease::alpha(theme::textDim, in * 0.8f));
}
