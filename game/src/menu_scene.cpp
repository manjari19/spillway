#include "menu_scene.h"

#include <algorithm>
#include <cmath>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "input.h"
#include "renderer.h"
#include "spillway/rng.h"
#include "theme.h"
#include "widgets.h"

using namespace spillway;

namespace {
const char* kTitle = "SPILLWAY";
constexpr float kTitleTracking = 0.06f;
const Vec2 kOutlet{14.72f, 2.3f};       // where the stream leaves the cliff
const Vec2 kOutletVelocity{-2.6f, 0.4f};
const Vec2 kDamCrest{10.1f, 4.7f};      // horizon glow sits behind this
} // namespace

MenuScene::MenuScene(App& app) : app_(app) {
    auto level = loadLevelFile(assets().levelDir + "menu.txt", /*requireGoals=*/false);
    LevelDesc desc;
    if (level) {
        desc = *level.level;
    } else {
        desc.size = {16, 9}; // keep the menu usable even without the backdrop file
        desc.water.push_back({{10, 4}, {14, 6}});
    }
    world_ = std::make_unique<World>(desc);
    targetParticles_ = world_->fluid().size() + 160;
    terrainView_.reset(world_->terrain());
    // Warm up so the menu opens with the spillway already running
    // (~5 simulated seconds, about 0.1 s of real time).
    for (int k = 0; k < 300; ++k) {
        world_->step();
        emit();
    }
    world_->events().clear();

    // A few slow glow motes drifting in the sky over the landscape.
    Rng rng(99);
    for (int i = 0; i < 22; ++i)
        motes_.push_back({{rng.uniform(7.0f, 15.5f), rng.uniform(0.4f, 5.0f)}, rng.uniform(0.0f, 6.28f),
                          rng.uniform(0.05f, 0.14f), rng.uniform(0.04f, 0.09f)});

    const Progress& pr = app_.progress();
    items_ = {{"Play", app_.levelCount() > 0}, {"Levels", true}, {"Settings", true}, {"Quit", true}};
    if (pr.anyProgress() && app_.levelCount() > 0) {
        const int next = pr.firstUnbeaten(app_.levelCount());
        const bool allDone = pr.beaten(app_.levelCount() - 1) && next == app_.levelCount() - 1;
        continueLine_ = allDone ? "All levels complete  \xC2\xB7  " + std::to_string(pr.totalStars()) + " of " +
                                      std::to_string(3 * app_.levelCount()) + " stars"
                                : "Continue: Level " + std::to_string(next + 1) + "  \xC2\xB7  " + app_.level(next).name;
    }
    lastMouse_ = input::mouse();
}

float MenuScene::titleY() const {
    // A fifth of the way down the part of the screen below any notch.
    const float top = ui::safeTop();
    return top + (static_cast<float>(GetScreenHeight()) - top) * 0.2f;
}

float MenuScene::itemHeight(int index) const {
    const float s = ui::scale();
    // Play gets a second line when there is progress to continue.
    return (index == 0 && !continueLine_.empty() ? 76.0f : 58.0f) * s;
}

float MenuScene::itemY(int index) const {
    const float s = ui::scale();
    const float titleH = measureText(FontRole::SemiBold, kTitle, theme::type::display * s, kTitleTracking).y;
    float y = titleY() + titleH + 86.0f * s;
    for (int i = 0; i < index; ++i) y += itemHeight(i);
    return y;
}

void MenuScene::activate(int index) {
    if (!items_[static_cast<size_t>(index)].enabled) return;
    const std::string& l = items_[static_cast<size_t>(index)].label;
    if (l == "Play") app_.startLevel(app_.progress().firstUnbeaten(app_.levelCount()));
    if (l == "Levels") app_.openLevelSelect();
    if (l == "Settings") app_.openSettings();
    if (l == "Quit") app_.quit();
}

void MenuScene::emit() {
    // Recycle: whatever reached the drain comes back out of the outlet, a
    // row of three at a time, once the previous row has moved clear.
    auto& f = world_->fluid();
    if (f.size() + 3 > targetParticles_) return;
    for (const Vec2 p : f.positions())
        if (lengthSq(p - kOutlet) < 0.2f * 0.2f) return;
    for (int k = -1; k <= 1; ++k) f.addParticle({kOutlet.x, kOutlet.y + static_cast<float>(k) * 0.13f}, kOutletVelocity);
}

void MenuScene::update(float dt) {
    time_ += dt;
    intro_ = std::min(1.0f, intro_ + dt / 0.9f);
    view_ = View::cover(world_->terrain().extent(),
                        {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});

    // Moving the mouse over the water stirs it gently.
    const Vector2 m = input::mouse();
    const float moved = std::hypot(m.x - lastMouse_.x, m.y - lastMouse_.y);
    lastMouse_ = m;
    if (moved > 1.0f) stir_ = 0.25f;
    stir_ = std::max(0.0f, stir_ - dt);
    Interaction in;
    in.point = view_.toWorld(m);
    in.radius = 1.0f;
    in.strength = -12.0f;
    in.active = stir_ > 0.0f;
    world_->fluid().setInteraction(in);
    const int steps = world_->update(dt);
    for (int k = 0; k < steps; ++k) emit();
    world_->events().clear();

    for (Mote& mo : motes_) {
        mo.p.x -= mo.speed * dt;
        mo.p.y += std::sin(time_ * 0.3f + mo.phase) * 0.03f * dt;
        if (mo.p.x < 6.5f) mo.p.x = 15.5f;
    }

    // Navigation: hover or up/down, enter/click to activate.
    const float s = ui::scale();
    const float textX = 0.075f * static_cast<float>(GetScreenWidth());
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const Rectangle r{textX - 24.0f * s, itemY(i) - 8.0f * s, 330.0f * s, itemHeight(i) - 6.0f * s};
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
    if (input::pressed(KEY_DOWN)) step(1);
    if (input::pressed(KEY_UP)) step(-1);
    if (input::pressed(KEY_ENTER) || input::pressed(KEY_SPACE)) activate(focus_);
    if (hovered >= 0 && input::mousePressed(MOUSE_BUTTON_LEFT)) activate(hovered);

    // The highlight pill slides to the focused item (ease-out approach).
    const float target = itemY(focus_);
    barY_ = barY_ < 0.0f ? target : ease::approach(barY_, target, 16.0f, dt);
}

void MenuScene::prepare() {
    terrainView_.sync(world_->terrain());
    terrainView_.prepare(app_.renderer(), view_);
}

void MenuScene::drawTitle(float alpha) const {
    const float s = ui::scale();
    const float x = 0.075f * static_cast<float>(GetScreenWidth());
    const float y = titleY();
    drawText(FontRole::SemiBold, kTitle, {x, y}, theme::type::display * s, ease::alpha(theme::text, alpha), kTitleTracking);
}

void MenuScene::drawWorld() {
    const Texture2D& blob = assets().blob;
    const Rectangle src{0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)};

    // Soft horizon glow behind the dam.
    const Vector2 c = view_.toScreen(kDamCrest);
    const float gr = 5.5f * view_.scale;
    DrawTexturePro(blob, src, {c.x - gr * 1.3f, c.y - gr * 0.55f, gr * 2.6f, gr * 1.1f}, {0, 0}, 0.0f,
                   ease::alpha(theme::violet, 0.16f));

    // Glow motes.
    Renderer::blendAdditive();
    for (const Mote& mo : motes_) {
        const Vector2 p = view_.toScreen(mo.p);
        const float r = mo.size * view_.scale * 2.0f;
        const float a = 0.10f + 0.08f * std::sin(time_ * 0.7f + mo.phase);
        DrawTexturePro(blob, src, {p.x - r, p.y - r, 2 * r, 2 * r}, {0, 0}, 0.0f, ease::alpha(Color{170, 150, 255, 255}, a));
    }
    Renderer::blendNormal();

    // The drain's glowing pool.
    for (const Rect& d : world_->level().drains) {
        const Vector2 a = view_.toScreen(d.min), b = view_.toScreen(d.max);
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.0f);
        DrawTexturePro(blob, src, {a.x - (b.x - a.x) * 0.4f, a.y - (b.y - a.y) * 3.0f, (b.x - a.x) * 1.8f, (b.y - a.y) * 5.0f},
                       {0, 0}, 0.0f, ease::alpha(theme::cyan, 0.22f + 0.08f * pulse));
    }

    app_.renderer().drawWater(world_->fluid().positions(), world_->fluid().velocities(), view_);
    terrainView_.draw(app_.renderer(), view_, world_->terrain());

    // Title in the world layer too, so the bloom gives it a soft glow (the
    // crisp copy is drawn in drawUi).
    drawTitle(0.5f * ease::outCubic(intro_));
}

void MenuScene::drawUi() {
    const float s = ui::scale();
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float x = 0.075f * sw;
    const float in = ease::outCubic(intro_);

    drawTitle(in);
    const float titleY = sh * 0.2f;
    const float titleH = measureText(FontRole::SemiBold, kTitle, theme::type::display * s, kTitleTracking).y;
    drawText(FontRole::Regular, "A fluid puzzle about guiding water", {x + 4.0f * s, titleY + titleH + 4.0f * s}, 20.0f * s,
             ease::alpha(ease::mix(theme::textDim, theme::text, 0.35f), in));

    // Fixed-width pill behind the focused item, sized to the text column.
    float colW = 0.0f;
    for (const Item& it : items_) colW = std::max(colW, measureText(FontRole::Medium, it.label, theme::type::heading * s).x);
    if (!continueLine_.empty()) colW = std::max(colW, measureText(FontRole::Regular, continueLine_, theme::type::small * s).x);
    const float padX = 22.0f * s;
    const Rectangle pill{x - padX, barY_ - 9.0f * s, colW + 2 * padX + 12.0f * s, itemHeight(focus_) - 10.0f * s};
    ui::glow(pill, 16.0f * s, 16.0f * s, ease::alpha(theme::violet, 0.35f * in), 0.0f);
    DrawRectangleRounded(pill, ui::roundness(pill, 16.0f * s), 24, ease::alpha(Color{70, 40, 170, 150}, in));
    DrawRectangleRoundedLinesEx(pill, ui::roundness(pill, 16.0f * s), 24, 1.0f, ease::alpha(Color{180, 160, 255, 70}, in));
    DrawRectangleRounded({pill.x + 9.0f * s, pill.y + pill.height * 0.25f, 3.0f * s, pill.height * 0.5f}, 1.0f, 6,
                         ease::alpha(theme::cyan, in));

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        const bool focused = i == focus_;
        const float h = ui::animate("menu:" + it.label, focused ? 1.0f : 0.0f, 12.0f);
        // Items slide in one after another on entry.
        const float appear = ease::outCubic(std::clamp((intro_ - 0.15f - 0.08f * static_cast<float>(i)) / 0.5f, 0.0f, 1.0f));
        const float ix = x + 8.0f * s * h - 18.0f * s * (1.0f - appear);
        const float y = itemY(i);
        const Color c = it.enabled ? ease::mix(theme::textDim, theme::text, h) : theme::textMuted;
        drawText(FontRole::Medium, it.label, {ix, y}, theme::type::heading * s, ease::alpha(c, appear));
        if (i == 0 && !continueLine_.empty()) {
            const float ly = y + measureText(FontRole::Medium, it.label, theme::type::heading * s).y + 2.0f * s;
            drawText(FontRole::Regular, continueLine_, {ix + 1.0f * s, ly}, theme::type::small * s,
                     ease::alpha(ease::mix(theme::textDim, theme::cyan, h * 0.6f), appear));
        }
    }

    // Footer.
    const float fs = theme::type::small * s;
    drawText(FontRole::Regular, "Up/Down and Enter, or click", {x, sh - 40.0f * s}, fs, ease::alpha(theme::textDim, in * 0.8f));
}
