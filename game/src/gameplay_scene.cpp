#include "gameplay_scene.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "renderer.h"
#include "theme.h"
#include "widgets.h"

using namespace spillway;

namespace {

template <class... Args>
std::string fmt(const char* f, Args... args) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}

Color toolColor(Tool t) {
    switch (t) {
    case Tool::Dig: return theme::warm;
    case Tool::Wall: return theme::wall;
    case Tool::Erase: return Color{200, 170, 230, 255};
    }
    return WHITE;
}

} // namespace

GameplayScene::GameplayScene(App& app, int levelIndex, LevelDesc level)
    : app_(app), levelIndex_(levelIndex), level_(std::move(level)) {
    world_ = std::make_unique<World>(level_);
    terrainView_.reset(world_->terrain());
    layout();
}

void GameplayScene::restart() { app_.goTo(app_.makeLevel(levelIndex_)); }
void GameplayScene::toMenu() { app_.goTo(app_.makeMenu()); }

void GameplayScene::layout() {
    const float s = ui::scale();
    const float m = 14.0f * s;
    hudBar_ = {m, m, static_cast<float>(GetScreenWidth()) - 2 * m, 60.0f * s};
    const float top = hudBar_.y + hudBar_.height + 12.0f * s;
    const Rectangle area{16.0f * s, top, static_cast<float>(GetScreenWidth()) - 32.0f * s,
                         static_cast<float>(GetScreenHeight()) - top - 16.0f * s};
    view_ = View::fit(world_->terrain().extent(), area);
}

float& GameplayScene::brushRadius() {
    switch (tool_) {
    case Tool::Dig: return digRadius_;
    case Tool::Wall: return wallRadius_;
    case Tool::Erase: return eraseRadius_;
    }
    return digRadius_;
}

bool GameplayScene::brushBlocked() const {
    if (tool_ != Tool::Wall) return false;
    if (world_->inkRemaining() <= 0) return true;
    // Walls are never placed on water, so show the brush as unavailable.
    const Vec2 m = view_.toWorld(GetMousePosition());
    const float r = wallRadius_ + world_->fluid().params().particleRadius;
    for (const Vec2 p : world_->fluid().positions())
        if (lengthSq(p - m) < r * r) return true;
    return false;
}

void GameplayScene::debugPause() {
    mode_ = Mode::Paused;
    card_ = 0.0f;
}

void GameplayScene::debugAutoSolve() {
    // Scripted solution for level 1: open the rim, then dig down to the drain.
    for (float y = 0.8f; y <= 2.7f; y += 0.2f) world_->queueEdit({Tool::Dig, {4.9f, y}, {6.3f, y}, 0.35f});
    world_->queueEdit({Tool::Dig, {14.0f, 4.9f}, {14.0f, 6.6f}, 0.45f});
    for (int s = 0; s < 60 * 60 && !world_->finished(); ++s) world_->step();
    world_->events().clear();
}

void GameplayScene::debugMidLevel() {
    // Rim opened and a short channel started: water mid-flow down the slope.
    for (float y = 0.8f; y <= 2.7f; y += 0.2f) world_->queueEdit({Tool::Dig, {4.9f, y}, {6.3f, y}, 0.35f});
    world_->queueEdit({Tool::Dig, {13.6f, 5.0f}, {14.2f, 5.9f}, 0.4f});
    for (int s = 0; s < 150; ++s) world_->step();
    world_->events().clear();
    time_ = 10.0f; // past the hint toast
}

void GameplayScene::handlePlayInput() {
    if (IsKeyPressed(KEY_ONE)) tool_ = Tool::Dig;
    if (IsKeyPressed(KEY_TWO)) tool_ = Tool::Wall;
    if (IsKeyPressed(KEY_THREE)) tool_ = Tool::Erase;
    if (IsKeyPressed(KEY_P)) debugPush_ = !debugPush_;

    // Brush size: wheel or [ ]. Each tool keeps its own size and limits.
    float& r = brushRadius();
    const float lo = tool_ == Tool::Wall ? 0.08f : 0.15f;
    const float hi = tool_ == Tool::Wall ? 0.25f : 0.8f;
    float delta = GetMouseWheelMove() * 0.03f;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) delta -= 0.03f;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) delta += 0.03f;
    r = std::clamp(r + delta, lo, hi);

    const Vec2 mouse = view_.toWorld(GetMousePosition());
    interaction_ = {};
    if (debugPush_) {
        // Debug only: pull (LMB) / push (RMB) the water.
        interaction_.point = mouse;
        interaction_.radius = 1.4f;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { interaction_.active = true; interaction_.strength = 40.0f; }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { interaction_.active = true; interaction_.strength = -40.0f; }
        stroking_ = false;
        return;
    }
    // A drag becomes a chain of segment edits, applied at the next fixed step.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        stroking_ = true;
        lastMouse_ = mouse;
        world_->queueEdit({tool_, mouse, mouse, r});
    } else if (stroking_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && lengthSq(mouse - lastMouse_) > 0.02f * 0.02f) {
        world_->queueEdit({tool_, lastMouse_, mouse, r});
        lastMouse_ = mouse;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) stroking_ = false;
}

void GameplayScene::spawnEffects() {
    WorldEvents& e = world_->events();
    for (const Vec2 p : e.drained) effects_.splash(p);
    for (const Vec2 p : e.lost) effects_.sink(p);
    for (const Vec2 p : e.dug) effects_.dust(p);
    terrainView_.markDug(world_->terrain(), e.dug, static_cast<float>(GetTime()));
    e.clear();
}

void GameplayScene::update(float dt) {
    time_ += dt;
    layout();

    // Global keys.
    if (IsKeyPressed(KEY_GRAVE)) showDebug_ = !showDebug_;
    if (IsKeyPressed(KEY_G)) showGrid_ = !showGrid_;
    if (IsKeyPressed(KEY_B)) {
        auto& f = world_->fluid();
        f.setNeighbourSearch(f.neighbourSearch() == NeighbourSearch::Grid ? NeighbourSearch::BruteForce : NeighbourSearch::Grid);
    }
    if (IsKeyPressed(KEY_R)) restart();

    if (mode_ == Mode::Playing) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            mode_ = Mode::Paused;
            focus_ = 0;
            stroking_ = false;
        } else {
            handlePlayInput();
        }
    } else if (mode_ == Mode::Paused && IsKeyPressed(KEY_ESCAPE)) {
        mode_ = Mode::Playing;
    }

    if (digDemoAt_ > 0.0f && time_ >= digDemoAt_) {
        world_->queueEdit({Tool::Dig, {7.5f, 3.6f}, {9.5f, 4.4f}, 0.4f});
        digDemoAt_ = -1.0f;
    }
    if (mode_ != Mode::Paused) {
        world_->fluid().setInteraction(mode_ == Mode::Playing ? interaction_ : Interaction{});
        world_->update(dt);
        int nodes = world_->lastSdfNodes();
        if (nodes > 0) lastSdfNodes_ = nodes;
        spawnEffects();
        effects_.update(dt);
    }

    if (mode_ == Mode::Playing && world_->finished()) {
        mode_ = Mode::Result;
        resultTime_ = 0.0f;
        focus_ = 0;
        stars_ = world_->stars();
    }
    if (mode_ == Mode::Result) resultTime_ += dt;

    // Cards ease in over ~0.35 s and out a little faster.
    const bool cardOpen = mode_ != Mode::Playing;
    card_ = std::clamp(card_ + (cardOpen ? dt / 0.35f : -dt / 0.25f), 0.0f, 1.0f);

    meter_ = ease::approach(meter_, world_->score().savedFraction(), 5.0f, dt);
    const float inkFrac = world_->inkBudget() > 0 ? static_cast<float>(world_->inkRemaining()) / static_cast<float>(world_->inkBudget()) : 0.0f;
    ink_ = ease::approach(ink_, inkFrac, 8.0f, dt);
}

void GameplayScene::prepare() {
    const int chunks = terrainView_.sync(world_->terrain());
    if (chunks > 0) lastChunks_ = chunks;
    terrainView_.prepare(app_.renderer(), view_);
}

// ---------------------------------------------------------------- world ---

void GameplayScene::drawDrainsAndPits() {
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.4f);
    for (const Rect& d : world_->level().drains) {
        const Vector2 a = view_.toScreen(d.min), b = view_.toScreen(d.max);
        const Rectangle r{a.x, a.y, b.x - a.x, b.y - a.y};
        // Light rising out of the grate, fading upward (hidden under dirt
        // until the player digs down to it).
        const float glowH = r.height * 2.2f;
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y - glowH), static_cast<int>(r.width),
                               static_cast<int>(glowH), Color{62, 232, 255, 0},
                               ease::alpha(theme::cyan, 0.16f + 0.08f * pulse));
        // The portal itself: a bright body with grate bars.
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                               static_cast<int>(r.height), ease::alpha(theme::cyan, 0.55f + 0.2f * pulse),
                               ease::alpha(theme::violet, 0.35f));
        const int bars = std::max(3, static_cast<int>(r.width / (14.0f * ui::scale())));
        for (int i = 1; i < bars; ++i) {
            const float x = r.x + r.width * static_cast<float>(i) / static_cast<float>(bars);
            DrawLineEx({x, r.y + 3}, {x, r.y + r.height}, 2.0f * ui::scale(), Color{10, 8, 30, 150});
        }
        DrawRectangleRec({r.x, r.y, r.width, 2.5f * ui::scale()}, ease::alpha(theme::wall, 0.75f + 0.25f * pulse));
    }
    for (const Rect& p : world_->level().pits) {
        const Vector2 a = view_.toScreen(p.min), b = view_.toScreen(p.max);
        const Rectangle r{a.x, a.y, b.x - a.x, b.y - a.y};
        // Dark void with a faint red-violet glow at its lip.
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width),
                               static_cast<int>(r.height), ease::alpha(theme::danger, 0.28f + 0.08f * pulse),
                               Color{3, 2, 8, 255});
        const float glowH = r.height * 0.8f;
        DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y - glowH), static_cast<int>(r.width),
                               static_cast<int>(glowH), Color{217, 74, 154, 0}, ease::alpha(theme::danger, 0.12f));
    }
}

void GameplayScene::drawBrush() {
    const Vector2 m = GetMousePosition();
    const float s = ui::scale();
    if (debugPush_) {
        const float r = 1.4f * view_.scale;
        DrawRing(m, r - 1.0f * s, r + 1.0f * s, 0, 360, 64, ease::alpha(theme::cyan, interaction_.active ? 0.7f : 0.35f));
        return;
    }
    const bool blocked = brushBlocked();
    const Color c = blocked ? Color{140, 136, 160, 255} : toolColor(tool_);
    const float r = brushRadius() * view_.scale;
    const float breathe = 1.0f + 0.03f * std::sin(time_ * 3.0f);

    // Soft inner wash using the blob texture, then a thin ring.
    const Texture2D& blob = assets().blob;
    const float g = r * 1.35f;
    DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                   {m.x - g, m.y - g, 2 * g, 2 * g}, {0, 0}, 0.0f, ease::alpha(c, blocked ? 0.06f : 0.12f));
    const float rr = r * breathe;
    const float w = 1.2f * s;
    if (tool_ == Tool::Erase || blocked) {
        // Dashed ring.
        const int dashes = std::max(12, static_cast<int>(rr / (4.0f * s)));
        const float step = 360.0f / static_cast<float>(dashes);
        for (int i = 0; i < dashes; ++i) {
            const float a0 = static_cast<float>(i) * step + time_ * 20.0f;
            DrawRing(m, rr - w, rr + w, a0, a0 + step * 0.55f, 4, ease::alpha(c, 0.85f));
        }
    } else {
        DrawRing(m, rr - w, rr + w, 0, 360, 72, ease::alpha(c, 0.9f));
    }
}

void GameplayScene::drawWorld() {
    // Open space uses the same full-screen background gradient as
    // everything else, so there is no visible letterbox around the level.
    drawDrainsAndPits();
    app_.renderer().drawWater(world_->fluid().positions(), world_->fluid().velocities(), view_);
    terrainView_.draw(app_.renderer(), view_, world_->terrain());
    effects_.draw(view_);

    if (showGrid_) {
        const auto& g = world_->fluid().grid();
        const float cs = g.cellSize() * view_.scale;
        for (int y = 0; y < g.cellsY(); ++y)
            for (int x = 0; x < g.cellsX(); ++x) {
                const Vector2 p = view_.toScreen(g.origin() + Vec2{x * g.cellSize(), y * g.cellSize()});
                const unsigned n = g.cellCount(x, y);
                if (n) DrawRectangleV(p, {cs, cs}, Color{62, 232, 255, static_cast<unsigned char>(std::min(90u, n * 6u))});
                DrawRectangleLinesEx({p.x, p.y, cs, cs}, 1.0f, Color{255, 255, 255, 10});
            }
    }

    if (mode_ == Mode::Playing && (IsCursorOnScreen() || forceCursor_)) drawBrush();
}

// ------------------------------------------------------------------- UI ---

void GameplayScene::drawHud() {
    const float s = ui::scale();
    const Rectangle bar = hudBar_;
    ui::glass(bar, 22.0f * s);
    const float cap = theme::type::caption * s;
    const float pad = 22.0f * s;

    // Left: level number and name.
    const std::string levelLabel = fmt("LEVEL %d", levelIndex_ + 1);
    const float nameSize = 20.0f * s;
    const Vector2 capM = measureText(FontRole::Medium, levelLabel, cap, 0.14f);
    const Vector2 nameM = measureText(FontRole::SemiBold, level_.name, nameSize);
    const float leftH = capM.y + 2.0f * s + nameM.y;
    const float ly = bar.y + (bar.height - leftH) * 0.5f;
    drawText(FontRole::Medium, levelLabel, {bar.x + pad, ly}, cap, theme::textDim, 0.14f);
    drawText(FontRole::SemiBold, level_.name, {bar.x + pad, ly + capM.y + 2.0f * s}, nameSize, theme::text);
    const float leftEnd = bar.x + pad + std::max(capM.x, nameM.x);

    // Right: ink, brush size, tool icons (laid out right to left).
    const float iconD = 32.0f * s, iconGap = 12.0f * s;
    const float keySize = 10.5f * s;
    const float keyH = measureText(FontRole::Medium, "1", keySize).y;
    // Icon and its key number stacked: they never overlap.
    const float iconTop = bar.y + (bar.height - (iconD + 3.0f * s + keyH)) * 0.5f;
    float x = bar.x + bar.width - pad + 6.0f * s;
    const Tool tools[] = {Tool::Erase, Tool::Wall, Tool::Dig};
    const char* keys[] = {"3", "2", "1"};
    for (int i = 0; i < 3; ++i) {
        x -= iconD;
        const Vector2 c{x + iconD * 0.5f, iconTop + iconD * 0.5f};
        const bool active = tools[i] == tool_ && !debugPush_;
        const float h = ui::animate(std::string("tool:") + keys[i], active ? 1.0f : 0.0f);
        const float rad = iconD * 0.5f;
        if (h > 0.01f) ui::glow({c.x - rad, c.y - rad, iconD, iconD}, rad, 10.0f * s, ease::alpha(theme::violet, 0.5f * h), 0.0f);
        DrawCircleV(c, rad, ease::mix(Color{255, 255, 255, 14}, Color{109, 60, 255, 220}, h));
        ui::toolIcon(tools[i], c, iconD * 0.62f, ease::mix(theme::textDim, theme::text, h));
        ui::textCentered(FontRole::Medium, keys[i], {c.x - rad, c.y + rad + 3.0f * s, iconD, keyH}, keySize,
                         ease::mix(theme::textMuted, theme::cyan, h));
        x -= iconGap;
    }
    x -= 12.0f * s;

    // Brush size (subtle).
    const std::string brushVal = debugPush_ ? std::string("push/pull") : fmt("%.2f m", brushRadius());
    const Vector2 bvM = measureText(FontRole::Medium, brushVal, theme::type::small * s);
    const Vector2 bcM = measureText(FontRole::Medium, "BRUSH", cap, 0.14f);
    const float brushW = std::max(bvM.x, bcM.x);
    x -= brushW;
    const float ry = bar.y + (bar.height - (bcM.y + 3.0f * s + bvM.y)) * 0.5f;
    drawText(FontRole::Medium, "BRUSH", {x, ry}, cap, theme::textDim, 0.14f);
    drawText(FontRole::Medium, brushVal, {x, ry + bcM.y + 3.0f * s}, theme::type::small * s,
             debugPush_ ? theme::cyan : theme::text);
    x -= 22.0f * s;

    // Ink.
    const bool hasInk = world_->inkBudget() > 0;
    const std::string inkVal = hasInk ? fmt("%.1f m", world_->inkMetres(world_->inkRemaining())) : std::string("none");
    const Vector2 icM = measureText(FontRole::Medium, "INK", cap, 0.14f);
    const Vector2 ivM = measureText(FontRole::Medium, inkVal, cap);
    const float inkW = std::max(96.0f * s, icM.x + 10.0f * s + ivM.x);
    x -= inkW;
    const float iy = bar.y + (bar.height - (icM.y + 8.0f * s + 6.0f * s)) * 0.5f;
    drawText(FontRole::Medium, "INK", {x, iy}, cap, theme::textDim, 0.14f);
    drawText(FontRole::Medium, inkVal, {x + inkW - ivM.x, iy}, cap, hasInk ? theme::text : theme::textMuted);
    ui::plainBar({x, iy + icM.y + 8.0f * s, inkW, 6.0f * s}, hasInk ? ink_ : 0.0f, theme::wall);
    const float rightStart = x;

    // Centre: saved meter, as wide as fits between the two sides.
    const float gap = 36.0f * s;
    const float avail = rightStart - leftEnd - 2 * gap;
    const float meterW = std::clamp(avail, 120.0f * s, 440.0f * s);
    float mx = bar.x + (bar.width - meterW) * 0.5f;
    mx = std::clamp(mx, leftEnd + gap, rightStart - gap - meterW);
    const int pct = static_cast<int>(std::floor(meter_ * 100.0f + 0.001f));
    const std::string savedVal = fmt("%d%%", pct);
    const std::string goal = fmt("GOAL %d%%", static_cast<int>(level_.winPercent));
    const Vector2 scM = measureText(FontRole::Medium, "SAVED", cap, 0.14f);
    const Vector2 svM = measureText(FontRole::SemiBold, savedVal, theme::type::small * s);
    const Vector2 gM = measureText(FontRole::Medium, goal, cap, 0.14f);
    const float barH = 10.0f * s;
    const float blockH = svM.y + 8.0f * s + barH;
    const float my = bar.y + (bar.height - blockH) * 0.5f;
    drawText(FontRole::Medium, "SAVED", {mx, my + (svM.y - scM.y) * 0.5f}, cap, theme::textDim, 0.14f);
    const bool reached = world_->score().saved >= world_->score().required;
    drawText(FontRole::SemiBold, savedVal, {mx + scM.x + 8.0f * s, my}, theme::type::small * s,
             reached ? theme::cyan : theme::text);
    drawText(FontRole::Medium, goal, {mx + meterW - gM.x, my + (svM.y - gM.y) * 0.5f}, cap, theme::textDim, 0.14f);
    ui::waterBar({mx, my + svM.y + 8.0f * s, meterW, barH}, meter_, level_.winPercent / 100.0f);
}

void GameplayScene::drawToasts() {
    const float s = ui::scale();
    const float top = hudBar_.y + hudBar_.height + 14.0f * s;
    auto toast = [&](const std::string& line1, const std::string& line2, float alpha) {
        if (alpha <= 0.01f) return;
        const Vector2 a = measureText(FontRole::Medium, line1, theme::type::body * s);
        const Vector2 b = measureText(FontRole::Regular, line2, theme::type::small * s);
        const float pad = 18.0f * s, gap = 4.0f * s;
        const float w = std::max(a.x, b.x) + 2 * pad, h = a.y + gap + b.y + 2 * pad * 0.8f;
        const float lift = (1.0f - alpha) * 8.0f * s;
        const Rectangle r{(static_cast<float>(GetScreenWidth()) - w) * 0.5f, top - lift, w, h};
        ui::glass(r, 18.0f * s, alpha);
        drawText(FontRole::Medium, line1, {r.x + (w - a.x) * 0.5f, r.y + pad * 0.8f}, theme::type::body * s,
                 ease::alpha(theme::text, alpha));
        drawText(FontRole::Regular, line2, {r.x + (w - b.x) * 0.5f, r.y + pad * 0.8f + a.y + gap},
                 theme::type::small * s, ease::alpha(theme::textDim, alpha));
    };

    if (mode_ != Mode::Playing) return;
    // Level hint for the first few seconds, then it fades away.
    const float hintA = ease::outCubic(std::clamp(time_ / 0.6f, 0.0f, 1.0f)) *
                        (1.0f - ease::outCubic(std::clamp((time_ - 6.0f) / 0.8f, 0.0f, 1.0f)));
    const float stallA = ui::animate("toast:stall", world_->stalled() ? 1.0f : 0.0f, 6.0f);
    if (stallA > 0.01f) {
        toast("The water has settled", "Press R to restart and try another route", stallA);
    } else if (!level_.hint.empty()) {
        toast(level_.hint, "1 Dig   2 Wall   3 Erase   Esc for controls", hintA);
    }
}

void GameplayScene::beginCard(Rectangle r) {
    const float t = ease::outCubic(card_);
    const float k = 0.96f + 0.04f * t;
    const Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlScalef(k, k, 1);
    rlTranslatef(-c.x, -c.y, 0);
}

void GameplayScene::endCard() {
    rlDrawRenderBatchActive();
    rlPopMatrix();
}

void GameplayScene::drawPauseCard() {
    const float s = ui::scale();
    const float a = ease::outCubic(card_);
    const float pad = 32.0f * s, gap = 10.0f * s, btnH = 48.0f * s;
    const float w = 380.0f * s;
    const std::string sub = fmt("Level %d  \xC2\xB7  %s", levelIndex_ + 1, level_.name.c_str());
    const Vector2 tM = measureText(FontRole::SemiBold, "Paused", theme::type::title * s);
    const Vector2 sM = measureText(FontRole::Regular, sub, theme::type::small * s);
    const std::string help1 = "1 Dig   2 Wall   3 Erase   Wheel or [ ] brush size";
    const std::string help2 = "R restart   Esc resume   ` debug overlay";
    const Vector2 h1 = measureText(FontRole::Regular, help1, theme::type::caption * s);
    const Vector2 h2 = measureText(FontRole::Regular, help2, theme::type::caption * s);
    const float cw = std::max({w, h1.x + 2 * pad, h2.x + 2 * pad, sM.x + 2 * pad});
    const float h = pad + tM.y + 4.0f * s + sM.y + 24.0f * s + 3 * btnH + 2 * gap + 24.0f * s + h1.y + 6.0f * s + h2.y + pad;
    const Rectangle r{(static_cast<float>(GetScreenWidth()) - cw) * 0.5f, (static_cast<float>(GetScreenHeight()) - h) * 0.5f, cw, h};

    beginCard(r);
    ui::glass(r, 28.0f * s, a);
    float y = r.y + pad;
    ui::textCentered(FontRole::SemiBold, "Paused", {r.x, y, r.width, tM.y}, theme::type::title * s, ease::alpha(theme::text, a));
    y += tM.y + 4.0f * s;
    ui::textCentered(FontRole::Regular, sub, {r.x, y, r.width, sM.y}, theme::type::small * s, ease::alpha(theme::textDim, a));
    y += sM.y + 24.0f * s;

    const char* labels[] = {"Resume", "Restart", "Main menu"};
    if (IsKeyPressed(KEY_DOWN)) focus_ = (focus_ + 1) % 3;
    if (IsKeyPressed(KEY_UP)) focus_ = (focus_ + 2) % 3;
    const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    for (int i = 0; i < 3; ++i) {
        const Rectangle b{r.x + pad, y, r.width - 2 * pad, btnH};
        const auto res = ui::button(std::string("pause:") + labels[i], b, labels[i], focus_ == i, enter, a, i == 0);
        if (res.hovered) focus_ = i;
        if (res.clicked && card_ > 0.5f) {
            if (i == 0) mode_ = Mode::Playing;
            if (i == 1) restart();
            if (i == 2) toMenu();
        }
        y += btnH + gap;
    }
    y += 24.0f * s - gap;
    DrawRectangleRec({r.x + pad, y - 12.0f * s, r.width - 2 * pad, 1.0f}, ease::alpha(Color{255, 255, 255, 20}, a));
    ui::textCentered(FontRole::Regular, help1, {r.x, y, r.width, h1.y}, theme::type::caption * s, ease::alpha(theme::textDim, a));
    y += h1.y + 6.0f * s;
    ui::textCentered(FontRole::Regular, help2, {r.x, y, r.width, h2.y}, theme::type::caption * s, ease::alpha(theme::textDim, a));
    endCard();
}

void GameplayScene::drawResultCard() {
    const float s = ui::scale();
    const float a = ease::outCubic(card_);
    const bool won = world_->status() == LevelStatus::Won;
    const float pad = 34.0f * s;
    const float w = 470.0f * s;

    const std::string title = won ? "Level complete" : "Out of water";
    const std::string sub = won ? level_.name : std::string("Not enough water can reach the drain");
    const Vector2 tM = measureText(FontRole::SemiBold, title, theme::type::title * s);
    const Vector2 sM = measureText(FontRole::Regular, sub, theme::type::small * s);
    const float starR = 25.0f * s;
    const float starsH = won ? starR * 2.0f + 26.0f * s : 0.0f;

    // Stats: label over value, three columns.
    const Score& sc = world_->score();
    const std::string saved = fmt("%d%%", static_cast<int>(std::floor(sc.savedFraction() * 100.0f)));
    const std::string goalV = fmt("%d%%", static_cast<int>(level_.winPercent));
    const std::string inkV = world_->inkBudget() > 0 ? fmt("%.1f m", world_->inkMetres(world_->inkUsed())) : std::string("none");
    const float valSize = 26.0f * s, cap = theme::type::caption * s;
    const float statH = measureText(FontRole::Medium, "X", cap, 0.14f).y + 4.0f * s + measureText(FontRole::SemiBold, "0", valSize).y;

    const bool hasNext = won && app_.levelExists(levelIndex_ + 1);
    std::vector<std::string> buttons{"Retry"};
    if (hasNext) buttons.push_back("Next");
    buttons.push_back("Menu");
    const float btnH = 48.0f * s, btnGap = 20.0f * s;
    // Tell the player what the next star needs.
    std::string starHint;
    if (won && stars_ < 3) {
        const bool thrifty = world_->inkBudget() <= 0 || world_->inkUsed() * 2 <= world_->inkBudget();
        if (stars_ == 1) starHint = fmt("Save %d%% for the second star", static_cast<int>(std::ceil(level_.twoStarPercent)));
        else if (!thrifty) starHint = fmt("Use at most half the ink and save %d%% for three stars",
                                          static_cast<int>(std::ceil(level_.threeStarPercent)));
        else starHint = fmt("Save %d%% for the third star", static_cast<int>(std::ceil(level_.threeStarPercent)));
    }
    const Vector2 hintM = starHint.empty() ? Vector2{0, 0} : measureText(FontRole::Regular, starHint, theme::type::small * s);
    const float hintH = starHint.empty() ? 0.0f : hintM.y + 14.0f * s;

    const float cw = std::max({w, tM.x + 2 * pad, sM.x + 2 * pad, hintM.x + 2 * pad});
    const float h = pad + tM.y + 4.0f * s + sM.y + 24.0f * s + starsH + hintH + statH + 28.0f * s + btnH + pad;
    const Rectangle r{(static_cast<float>(GetScreenWidth()) - cw) * 0.5f, (static_cast<float>(GetScreenHeight()) - h) * 0.5f, cw, h};

    beginCard(r);
    ui::glass(r, 28.0f * s, a);
    float y = r.y + pad;
    ui::textCentered(FontRole::SemiBold, title, {r.x, y, r.width, tM.y}, theme::type::title * s,
                     ease::alpha(won ? theme::text : theme::danger, a));
    y += tM.y + 4.0f * s;
    ui::textCentered(FontRole::Regular, sub, {r.x, y, r.width, sM.y}, theme::type::small * s, ease::alpha(theme::textDim, a));
    y += sM.y + 24.0f * s;

    if (won) {
        // Stars pop in one by one with a small overshoot.
        const float cy = y + starR;
        for (int i = 0; i < 3; ++i) {
            const float cx = r.x + r.width * 0.5f + static_cast<float>(i - 1) * (starR * 2.0f + 18.0f * s);
            const float t0 = 0.35f + 0.22f * static_cast<float>(i);
            const float t = std::clamp((resultTime_ - t0) / 0.4f, 0.0f, 1.0f);
            const float pop = ease::outBack(t);
            const float lift = i == 1 ? -6.0f * s : 0.0f; // middle star sits a touch higher
            ui::star({cx, cy + lift}, starR * std::max(0.0f, pop), i < stars_ ? 1.0f : 0.0f, a * std::min(1.0f, t * 3.0f));
            // Faint empty outline before it pops.
            if (t <= 0.0f) ui::star({cx, cy + lift}, starR, 0.0f, a * 0.6f);
        }
        y += starsH;
        if (!starHint.empty()) {
            const float ha = a * std::clamp((resultTime_ - 1.1f) / 0.4f, 0.0f, 1.0f);
            ui::textCentered(FontRole::Regular, starHint, {r.x, y - 8.0f * s, r.width, hintM.y}, theme::type::small * s,
                             ease::alpha(theme::textDim, ha));
            y += hintH;
        }
    }

    const char* labels[] = {"SAVED", "GOAL", "INK USED"};
    const std::string values[] = {saved, goalV, inkV};
    const float colW = (r.width - 2 * pad) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const Rectangle col{r.x + pad + colW * static_cast<float>(i), y, colW, statH};
        const Vector2 lm = measureText(FontRole::Medium, labels[i], cap, 0.14f);
        drawText(FontRole::Medium, labels[i], {col.x + (colW - lm.x) * 0.5f, y}, cap, ease::alpha(theme::textDim, a), 0.14f);
        ui::textCentered(FontRole::SemiBold, values[i], {col.x, y + lm.y + 4.0f * s, colW, statH - lm.y - 4.0f * s}, valSize,
                         ease::alpha(i == 0 && won ? theme::cyan : theme::text, a));
    }
    y += statH + 28.0f * s;

    const int n = static_cast<int>(buttons.size());
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_DOWN)) focus_ = (focus_ + 1) % n;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_UP)) focus_ = (focus_ + n - 1) % n;
    const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    const float bw = (r.width - 2 * pad - btnGap * static_cast<float>(n - 1)) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const Rectangle b{r.x + pad + (bw + btnGap) * static_cast<float>(i), y, bw, btnH};
        const bool primary = buttons[static_cast<size_t>(i)] == (hasNext ? "Next" : "Retry");
        const auto res = ui::button("result:" + buttons[static_cast<size_t>(i)], b, buttons[static_cast<size_t>(i)],
                                    focus_ == i, enter, a, primary);
        if (res.hovered) focus_ = i;
        if (res.clicked && card_ > 0.5f) {
            const std::string& l = buttons[static_cast<size_t>(i)];
            if (l == "Retry") restart();
            if (l == "Next") app_.goTo(app_.makeLevel(levelIndex_ + 1));
            if (l == "Menu") toMenu();
        }
    }
    endCard();
}

void GameplayScene::drawDebug() {
    const float s = ui::scale();
    const auto& f = world_->fluid();
    struct Row { const char* label; std::string value; };
    const Row rows[] = {
        {"fps", fmt("%d", GetFPS())},
        {"physics step", fmt("%.2f ms", world_->lastStepMs())},
        {"particles", fmt("%zu", f.size())},
        {"neighbour search", f.neighbourSearch() == NeighbourSearch::Grid ? "grid" : "brute force"},
        {"threads", "1"},
        {"sdf nodes / edit", fmt("%d", lastSdfNodes_)},
        {"mesh chunks / edit", fmt("%d", lastChunks_)},
        {"push/pull (P)", debugPush_ ? "on" : "off"},
    };
    const float ls = theme::type::caption * s, vs = 12.5f * s;
    float labelW = 0, valueW = 0, rowH = 0;
    for (const Row& r : rows) {
        const Vector2 lm = measureText(FontRole::Regular, r.label, ls);
        const Vector2 vm = measureText(FontRole::Mono, r.value, vs);
        labelW = std::max(labelW, lm.x);
        valueW = std::max(valueW, vm.x);
        rowH = std::max({rowH, lm.y, vm.y});
    }
    const std::string keys = "`  hide    G  grid    B  search";
    const Vector2 km = measureText(FontRole::Regular, keys, ls);
    const float pad = 14.0f * s, lineGap = 4.0f * s, colGap = 24.0f * s;
    const int n = static_cast<int>(std::size(rows));
    const float w = std::max(labelW + colGap + valueW, km.x) + 2 * pad;
    const float h = pad + static_cast<float>(n) * (rowH + lineGap) + 6.0f * s + km.y + pad;
    const Rectangle r{16.0f * s, static_cast<float>(GetScreenHeight()) - 16.0f * s - h, w, h};
    ui::glass(r, 16.0f * s, 0.95f);
    float y = r.y + pad;
    for (const Row& row : rows) {
        drawText(FontRole::Regular, row.label, {r.x + pad, y}, ls, theme::textDim);
        const Vector2 vm = measureText(FontRole::Mono, row.value, vs);
        drawText(FontRole::Mono, row.value, {r.x + r.width - pad - vm.x, y + (rowH - vm.y) * 0.5f}, vs, theme::text);
        y += rowH + lineGap;
    }
    drawText(FontRole::Regular, keys, {r.x + pad, y + 6.0f * s}, ls, theme::textMuted);
}

void GameplayScene::drawUi() {
    drawHud();
    drawToasts();
    if (showDebug_) drawDebug();
    if (card_ > 0.001f) {
        const float a = ease::outCubic(card_);
        app_.renderer().drawBackdrop(a);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), ease::alpha(Color{5, 4, 13, 255}, 0.35f * a));
        if (mode_ == Mode::Result || (mode_ == Mode::Playing && world_->finished())) drawResultCard();
        else drawPauseCard();
    }
}
