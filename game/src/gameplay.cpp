#include "gameplay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui.h"

using namespace spillway;

namespace {

constexpr Color kSlow{0x6d, 0x3c, 0xff, 255}; // still water: deep violet
constexpr Color kFast{0x3e, 0xe8, 0xff, 255}; // fast water: bright cyan

Color lerpColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

// Cheap integer hash -> [0, 1) for per-cell dirt texture variation.
float hash01(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffff) / 65536.0f;
}

const char* toolName(Tool t) {
    switch (t) {
    case Tool::Dig: return "Dig";
    case Tool::Wall: return "Wall";
    case Tool::Erase: return "Erase";
    }
    return "";
}

std::string fmt(const char* f, auto... args) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}

} // namespace

Gameplay::Gameplay(LevelDesc level) : level_(std::move(level)) { restart(); }

Gameplay::~Gameplay() {
    if (terrainTex_.id) UnloadTexture(terrainTex_);
    if (terrainImg_.data) UnloadImage(terrainImg_);
}

void Gameplay::restart() {
    world_ = std::make_unique<World>(level_);
    stroking_ = false;
    paused_ = false;
    lastEditSdfNodes_ = 0;
    terrainVersion_ = ~0ull;

    const Terrain& t = world_->terrain();
    if (!terrainImg_.data || terrainImg_.width != t.width() || terrainImg_.height != t.height()) {
        if (terrainTex_.id) UnloadTexture(terrainTex_);
        if (terrainImg_.data) UnloadImage(terrainImg_);
        terrainImg_ = GenImageColor(t.width(), t.height(), BLANK);
        terrainTex_ = LoadTextureFromImage(terrainImg_);
        // Bilinear filtering softens the cell edges when the texture is
        // stretched over the level (proper smooth edges come in M3).
        SetTextureFilter(terrainTex_, TEXTURE_FILTER_BILINEAR);
    }
}

void Gameplay::fitView() {
    const float s = ui::scale();
    // Leave room at the top and bottom for HUD panels.
    const float top = 100.0f * s, bottom = 84.0f * s, side = 16.0f * s;
    const float availW = static_cast<float>(GetScreenWidth()) - 2 * side;
    const float availH = static_cast<float>(GetScreenHeight()) - top - bottom;
    const Vec2 size = world_->terrain().extent();
    view_.scale = std::min(availW / size.x, availH / size.y);
    view_.offset = {(static_cast<float>(GetScreenWidth()) - size.x * view_.scale) * 0.5f,
                    top + (availH - size.y * view_.scale) * 0.5f};
}

float& Gameplay::brushRadius() {
    switch (tool_) {
    case Tool::Dig: return digRadius_;
    case Tool::Wall: return wallRadius_;
    case Tool::Erase: return eraseRadius_;
    }
    return digRadius_;
}

bool Gameplay::waterUnderBrush() const {
    const Vec2 m = view_.toWorld(GetMousePosition());
    const float r = (tool_ == Tool::Wall ? wallRadius_ : 0.0f) + world_->fluid().params().particleRadius;
    for (const Vec2 p : world_->fluid().positions())
        if (lengthSq(p - m) < r * r) return true;
    return false;
}

void Gameplay::handleInput() {
    if (IsKeyPressed(KEY_R)) restart();
    if (IsKeyPressed(KEY_ESCAPE)) paused_ = !paused_;
    if (IsKeyPressed(KEY_GRAVE)) showDebug_ = !showDebug_;
    if (IsKeyPressed(KEY_G)) showGrid_ = !showGrid_;
    if (IsKeyPressed(KEY_P)) debugPush_ = !debugPush_;
    if (IsKeyPressed(KEY_B)) {
        auto& f = world_->fluid();
        f.setNeighbourSearch(f.neighbourSearch() == NeighbourSearch::Grid ? NeighbourSearch::BruteForce
                                                                           : NeighbourSearch::Grid);
    }
    if (paused_) { stroking_ = false; return; }

    if (IsKeyPressed(KEY_ONE)) tool_ = Tool::Dig;
    if (IsKeyPressed(KEY_TWO)) tool_ = Tool::Wall;
    if (IsKeyPressed(KEY_THREE)) tool_ = Tool::Erase;

    // Brush size: wheel or [ ]. Each tool keeps its own size and limits.
    float& r = brushRadius();
    const float lo = tool_ == Tool::Wall ? 0.08f : 0.15f;
    const float hi = tool_ == Tool::Wall ? 0.25f : 0.8f;
    float delta = GetMouseWheelMove() * 0.03f;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) delta -= 0.03f;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) delta += 0.03f;
    r = std::clamp(r + delta, lo, hi);

    const Vec2 mouse = view_.toWorld(GetMousePosition());

    // Debug only: pull (LMB) / push (RMB) the water.
    interaction_ = {};
    if (debugPush_) {
        interaction_.point = mouse;
        interaction_.radius = 1.4f;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { interaction_.active = true; interaction_.strength = 40.0f; }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { interaction_.active = true; interaction_.strength = -40.0f; }
        stroking_ = false;
        return;
    }

    // Tools: a drag becomes a chain of segment edits. They are queued and
    // applied at the start of the next fixed physics step.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        stroking_ = true;
        lastMouse_ = mouse;
        world_->queueEdit({tool_, mouse, mouse, r});
    } else if (stroking_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (lengthSq(mouse - lastMouse_) > 0.02f * 0.02f) {
            world_->queueEdit({tool_, lastMouse_, mouse, r});
            lastMouse_ = mouse;
        }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) stroking_ = false;
}

void Gameplay::update(float frameSeconds) {
    fitView();
    handleInput();
    world_->fluid().setInteraction(interaction_);
    if (!paused_) {
        world_->update(frameSeconds);
        if (world_->lastSdfNodes() > 0) lastEditSdfNodes_ = world_->lastSdfNodes();
    }
}

void Gameplay::refreshTerrainTexture() {
    const Terrain& t = world_->terrain();
    if (t.version() == terrainVersion_) return;
    terrainVersion_ = t.version();

    const Color dirtA{88, 58, 78, 255}, dirtB{70, 45, 66, 255};
    const Color rock{58, 62, 92, 255};
    const Color wall{236, 232, 255, 255};
    auto* px = static_cast<Color*>(terrainImg_.data);
    for (int y = 0; y < t.height(); ++y) {
        for (int x = 0; x < t.width(); ++x) {
            Color c;
            switch (t.at(x, y)) {
            case Cell::Empty: c = {dirtB.r, dirtB.g, dirtB.b, 0}; break; // rgb avoids dark fringes
            case Cell::Dirt: c = lerpColor(dirtA, dirtB, hash01(x / 2, y / 2)); break;
            case Cell::Rock: c = lerpColor(rock, Color{48, 50, 78, 255}, hash01(x / 3, y / 3)); break;
            case Cell::Wall: c = wall; break;
            }
            px[y * t.width() + x] = c;
        }
    }
    UpdateTexture(terrainTex_, terrainImg_.data);
}

void Gameplay::drawWorld() {
    const Vec2 size = world_->terrain().extent();
    const Vector2 o = view_.toScreen({0, 0});
    const Rectangle area{o.x, o.y, size.x * view_.scale, size.y * view_.scale};
    DrawRectangleRec(area, Color{14, 12, 32, 255});

    // Drains and pits sit behind the terrain so their edges hide under dirt.
    for (const Rect& d : world_->level().drains) {
        const Vector2 a = view_.toScreen(d.min), b = view_.toScreen(d.max);
        DrawRectangleGradientV(static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x - a.x),
                               static_cast<int>(b.y - a.y), Color{62, 232, 255, 20}, Color{62, 232, 255, 90});
        DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 2.0f, Color{62, 232, 255, 160});
    }
    for (const Rect& p : world_->level().pits) {
        const Vector2 a = view_.toScreen(p.min), b = view_.toScreen(p.max);
        DrawRectangleGradientV(static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x - a.x),
                               static_cast<int>(b.y - a.y), Color{255, 80, 120, 20}, Color{255, 80, 120, 90});
        DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 2.0f, Color{255, 80, 120, 160});
    }

    refreshTerrainTexture();
    DrawTexturePro(terrainTex_, {0, 0, static_cast<float>(terrainTex_.width), static_cast<float>(terrainTex_.height)},
                   area, {0, 0}, 0.0f, WHITE);

    if (showGrid_) {
        const auto& g = world_->fluid().grid();
        const float cs = g.cellSize() * view_.scale;
        for (int y = 0; y < g.cellsY(); ++y) {
            for (int x = 0; x < g.cellsX(); ++x) {
                const Vector2 s = view_.toScreen(g.origin() + Vec2{x * g.cellSize(), y * g.cellSize()});
                const unsigned a = std::min(120u, g.cellCount(x, y) * 8u);
                if (a) DrawRectangleV(s, {cs, cs}, Color{62, 232, 255, static_cast<unsigned char>(a)});
                DrawRectangleLinesEx({s.x, s.y, cs, cs}, 1.0f, Color{255, 255, 255, 10});
            }
        }
    }

    const auto pos = world_->fluid().positions();
    const auto vel = world_->fluid().velocities();
    const float radius = std::max(1.5f, 0.075f * view_.scale);
    for (size_t i = 0; i < pos.size(); ++i)
        DrawCircleV(view_.toScreen(pos[i]), radius, lerpColor(kSlow, kFast, length(vel[i]) / 5.0f));
}

void Gameplay::drawBrush() {
    const Vector2 m = GetMousePosition();
    if (debugPush_) {
        const float r = 1.4f * view_.scale;
        DrawCircleV(m, r, Color{255, 255, 255, interaction_.active ? (unsigned char)18 : (unsigned char)8});
        DrawCircleLinesV(m, r, Color{255, 255, 255, 70});
        return;
    }
    Color c = tool_ == Tool::Dig ? Color{255, 190, 120, 255} : tool_ == Tool::Wall ? Color{240, 236, 255, 255}
                                                                                   : Color{255, 120, 160, 255};
    if (tool_ == Tool::Wall && (world_->inkRemaining() <= 0 || waterUnderBrush())) c = Color{130, 125, 150, 255};
    const float r = brushRadius() * view_.scale;
    // Soft glow: stacked translucent discs fading outward, then a thin ring.
    for (int k = 4; k >= 1; --k) {
        const float rr = r * (1.0f + 0.12f * static_cast<float>(k));
        DrawCircleV(m, rr, Color{c.r, c.g, c.b, static_cast<unsigned char>(10)});
    }
    DrawCircleV(m, r, Color{c.r, c.g, c.b, 28});
    DrawRing(m, r - 1.5f, r + 0.5f, 0, 360, 48, Color{c.r, c.g, c.b, 200});
}

void Gameplay::drawHud() {
    const float s = ui::scale();
    const float margin = 16.0f * s;
    const float pad = 16.0f * s;
    const float big = 24.0f * s, mid = 18.0f * s, small = 15.0f * s;

    // Top-left: level title and hint.
    std::vector<ui::Line> title{{level_.name, big, ui::colors::text}};
    if (!level_.hint.empty()) title.push_back({level_.hint, small, ui::colors::dim});
    const Rectangle titleRect = ui::textPanel(title, ui::Anchor::TopLeft, margin);

    // Top-right: saved meter with the target marked.
    const Score& sc = world_->score();
    const std::string savedText = fmt("Saved %d%%", static_cast<int>(std::floor(sc.savedFraction() * 100.0f)));
    const std::string targetText = fmt("target %d%%", static_cast<int>(level_.winPercent));
    const Vector2 st = ui::measureText(savedText, mid);
    const Vector2 tt = ui::measureText(targetText, small);
    const float barW = std::max(260.0f * s, st.x + tt.x + 24.0f * s);
    const float barH = 10.0f * s;
    const Rectangle meter = ui::place({barW + 2 * pad, st.y + 12.0f * s + barH + 2 * pad}, ui::Anchor::TopRight, margin);
    ui::drawPanel(meter);
    ui::drawText(savedText, {meter.x + pad, meter.y + pad}, mid, ui::colors::text);
    ui::drawText(targetText, {meter.x + meter.width - pad - tt.x, meter.y + pad + (st.y - tt.y) * 0.5f}, small,
                 ui::colors::dim);
    const bool reached = sc.saved >= sc.required;
    ui::drawBar({meter.x + pad, meter.y + pad + st.y + 12.0f * s, barW, barH}, sc.savedFraction(),
                reached ? ui::colors::good : ui::colors::accent, level_.winPercent / 100.0f);

    // Bottom-left: one row with tools, brush size and ink.
    const char* keys[] = {"1", "2", "3"};
    const Tool tools[] = {Tool::Dig, Tool::Wall, Tool::Erase};
    const float chipPadX = 10.0f * s, chipGap = 8.0f * s, sectionGap = 20.0f * s;
    const float chipH = mid + 10.0f * s;
    const std::string brushText = debugPush_ ? std::string("Debug push/pull ON (P)") : fmt("Brush %.2f m", brushRadius());
    const float inkM = world_->inkMetres(world_->inkRemaining());
    const float inkTotal = world_->inkMetres(world_->inkBudget());
    const bool hasInk = world_->inkBudget() > 0;
    const std::string inkText = hasInk ? fmt("Ink %.1f / %.1f m", inkM, inkTotal) : "No wall ink";
    const float inkBarW = hasInk ? 90.0f * s : 0.0f;

    float rowW = 0.0f;
    float chipW[3];
    for (int i = 0; i < 3; ++i) {
        chipW[i] = ui::measureText(fmt("%s  %s", keys[i], toolName(tools[i])), mid).x + 2 * chipPadX;
        rowW += chipW[i] + (i < 2 ? chipGap : 0.0f);
    }
    const Vector2 bt = ui::measureText(brushText, small), it = ui::measureText(inkText, small);
    rowW += sectionGap + bt.x + sectionGap + it.x + (hasInk ? 10.0f * s + inkBarW : 0.0f);
    const Rectangle tp = ui::place({rowW + 2 * pad, chipH + 2 * pad}, ui::Anchor::BottomLeft, margin);
    ui::drawPanel(tp);
    float x = tp.x + pad;
    const float rowY = tp.y + pad;
    for (int i = 0; i < 3; ++i) {
        const Rectangle chip{x, rowY, chipW[i], chipH};
        const bool active = tools[i] == tool_ && !debugPush_;
        DrawRectangleRounded(chip, 0.5f, 8, active ? Color{109, 60, 255, 170} : Color{255, 255, 255, 14});
        ui::drawText(fmt("%s  %s", keys[i], toolName(tools[i])), {x + chipPadX, rowY + (chipH - mid) * 0.5f}, mid,
                     active ? ui::colors::text : ui::colors::dim);
        x += chipW[i] + chipGap;
    }
    x += sectionGap - chipGap;
    ui::drawText(brushText, {x, rowY + (chipH - bt.y) * 0.5f}, small, debugPush_ ? ui::colors::accent : ui::colors::dim);
    x += bt.x + sectionGap;
    ui::drawText(inkText, {x, rowY + (chipH - it.y) * 0.5f}, small, ui::colors::dim);
    x += it.x + 10.0f * s;
    if (hasInk) {
        ui::drawBar({x, rowY + (chipH - 8.0f * s) * 0.5f, inkBarW, 8.0f * s},
                    static_cast<float>(world_->inkRemaining()) / static_cast<float>(world_->inkBudget()),
                    Color{236, 232, 255, 220});
    }

    // Bottom-right: key reminder.
    ui::textPanel({{"Wheel/[ ] brush   R restart   Esc pause   ` debug", small, ui::colors::dim}},
                  ui::Anchor::BottomRight, margin);

    meterBottom_ = meter.y + meter.height;
    drawOutcome(std::max(titleRect.y + titleRect.height, meter.y + meter.height) + margin);
}

void Gameplay::drawOutcome(float topY) {
    const float s = ui::scale();
    const Score& sc = world_->score();
    const int pct = static_cast<int>(std::floor(sc.savedFraction() * 100.0f));
    const LevelStatus status = world_->status();

    if (status != LevelStatus::Playing || paused_) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{5, 4, 13, 140});
        std::vector<ui::Line> lines;
        if (paused_) {
            lines = {{"Paused", 40.0f * s, ui::colors::text}, {"Esc to resume   R to restart", 18.0f * s, ui::colors::dim}};
        } else if (status == LevelStatus::Won) {
            lines = {{"Level complete", 40.0f * s, ui::colors::good},
                     {fmt("You saved %d%% of the water (needed %d%%)", pct, static_cast<int>(level_.winPercent)),
                      20.0f * s, ui::colors::text},
                     {"R to play again", 18.0f * s, ui::colors::dim}};
        } else {
            lines = {{"Not enough water left", 40.0f * s, ui::colors::bad},
                     {fmt("Saved %d%%, needed %d%%", pct, static_cast<int>(level_.winPercent)), 20.0f * s,
                      ui::colors::text},
                     {"R to restart", 18.0f * s, ui::colors::dim}};
        }
        ui::textPanel(lines, ui::Anchor::Center, 0.0f);
    } else if (world_->stalled()) {
        const std::vector<ui::Line> lines{{"The water has settled. Press R to restart?", 18.0f * s, ui::colors::text}};
        ui::textPanel(lines, ui::Anchor::TopCenter, 0.0f, {0, topY});
    }
}

void Gameplay::drawDebug() {
    const float s = ui::scale();
    const auto& f = world_->fluid();
    const float size = 15.0f * s;
    const std::vector<ui::Line> lines{
        {fmt("FPS %d", GetFPS()), size, ui::colors::text},
        {fmt("physics step %.2f ms", world_->lastStepMs()), size, ui::colors::text},
        {fmt("particles %zu", f.size()), size, ui::colors::text},
        {fmt("neighbour search: %s", f.neighbourSearch() == NeighbourSearch::Grid ? "grid" : "brute force"), size,
         ui::colors::text},
        {"threads 1", size, ui::colors::text},
        {fmt("SDF nodes rebuilt by last edit: %d", lastEditSdfNodes_), size, ui::colors::text},
        {fmt("debug push/pull: %s", debugPush_ ? "on" : "off"), size, ui::colors::text},
        {"`  hide    G  grid    B  search    P  push/pull", size, ui::colors::dim},
    };
    // Top-right, under the saved meter (over the sky, away from the drain).
    ui::textPanel(lines, ui::Anchor::TopRight, 16.0f * s, {0, meterBottom_ - 6.0f * s});
}

void Gameplay::setDebugOverlay(bool on) { showDebug_ = on; }

void Gameplay::draw() {
    drawWorld();
    if (!paused_ && world_->status() == LevelStatus::Playing) drawBrush();
    drawHud();
    if (showDebug_) drawDebug();
}
