#include "level_select_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "input.h"
#include "renderer.h"
#include "theme.h"
#include "widgets.h"

using namespace spillway;

namespace {

constexpr float kPi = 3.14159265f;

// The water in the channel is drawn by the game's metaball renderer, which
// works in metres. These "metres" are only for that: rest spacing along the
// channel and between its three rows, in the same units as real water.
constexpr float kAlong = 0.12f;
constexpr float kAcross = 0.115f;
// Logical px per "metre", chosen so the water fills ~60% of the channel.
float waterScale(float channelW) { return channelW * 0.6f / 0.43f; }

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

// Breaks text into lines no wider than maxW.
std::vector<std::string> wrap(FontRole role, const std::string& text, float size, float maxW) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string word, line;
    while (in >> word) {
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measureText(role, trial, size).x > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// A thick, round-jointed stroke along a polyline (opaque colours only, so
// overlapping joints don't double up).
void stroke(const std::vector<Vector2>& pts, size_t count, float width, Color c) {
    count = std::min(count, pts.size());
    for (size_t i = 1; i < count; ++i) DrawLineEx(pts[i - 1], pts[i], width, c);
    for (size_t i = 0; i < count; i += 2) DrawCircleV(pts[i], width * 0.5f, c);
    if (count > 0) DrawCircleV(pts[count - 1], width * 0.5f, c);
}

// Soft additive glow made of the shared blob texture.
void glowAt(Vector2 c, float radius, Color color) {
    const Texture2D& blob = assets().blob;
    DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                   {c.x - radius, c.y - radius, 2 * radius, 2 * radius}, {0, 0}, 0.0f, color);
}

} // namespace

LevelSelectScene::TipMetrics LevelSelectScene::tipMetrics(int i) const {
    const float s = ui::scale();
    TipMetrics m;
    m.w = std::min(300.0f * s, static_cast<float>(GetScreenWidth()) - 32.0f * s);
    m.pad = 14.0f * s;
    m.thumbW = m.w - 2.0f * m.pad;
    m.thumbH = m.thumbW * 9.0f / 16.0f;
    m.capS = theme::type::caption * s;
    m.nameS = theme::type::body * s;
    m.hintS = theme::type::small * s;
    m.capM = measureText(FontRole::Medium, "LEVEL 00", m.capS, 0.14f);
    m.nameM = measureText(FontRole::SemiBold, app_.level(i).name, m.nameS);
    m.hint = wrap(FontRole::Regular, app_.level(i).hint, m.hintS, m.thumbW);
    m.lineH = measureText(FontRole::Regular, "Ag", m.hintS).y + 2.0f * s;
    m.statsH = 18.0f * s;
    m.h = m.pad + m.thumbH + 12.0f * s + m.capM.y + 3.0f * s + m.nameM.y + 6.0f * s +
          m.lineH * static_cast<float>(m.hint.size()) + 10.0f * s + m.statsH + m.pad;
    return m;
}

LevelSelectScene::LevelSelectScene(App& app) : app_(app) {
    const int n = app_.levelCount();
    const Progress& pr = app_.progress();
    bool allDone = n > 0;
    for (int i = 0; i < n; ++i) allDone = allDone && pr.beaten(i);
    focus_ = std::clamp(pr.firstUnbeaten(n), 0, std::max(0, n - 1));
    // The water reaches the current level, or the end pool once all are done.
    fillTo_ = allDone ? n : focus_;
    const int shown = app_.pathFillShown();
    fillFrom_ = shown >= 0 && shown < fillTo_ ? shown : fillTo_;
    app_.setPathFillShown(fillTo_);
    tipFor_ = focus_;
    layout();
}

void LevelSelectScene::layout() {
    const float s = ui::scale();
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    layoutW_ = sw;
    layoutH_ = sh;
    const int n = std::max(2, app_.levelCount());

    channelW_ = 26.0f * s;
    nodeR_ = 23.0f * s;
    safeTop_ = ui::safeTop();
    headerBottom_ = safeTop_ + 116.0f * s;
    const float footer = 54.0f * s;

    // Nodes evenly spaced across the width on a gentle sine: 1.25 waves.
    // The channel starts at a spring left of the first node and ends in a
    // pool right of the last one.
    const float xA = 112.0f * s, xB = sw - 112.0f * s;
    // Vertical stack: header, a band for the tooltip (as tall as the
    // tallest one), then the river. Tooltips always open in that band, above
    // the whole river, so they can never cover a node. The stack is centred
    // in the space between header and footer.
    tipBand_ = 0.0f;
    for (int i = 0; i < app_.levelCount(); ++i) tipBand_ = std::max(tipBand_, tipMetrics(i).h);
    const float gap = 14.0f * s, stars = 26.0f * s;
    const float avail = sh - footer - headerBottom_;
    const float amp = std::clamp((avail - tipBand_ - gap - 2.0f * nodeR_ - stars) * 0.5f, 20.0f * s, 100.0f * s);
    const float stack = tipBand_ + gap + nodeR_ + 2.0f * amp + nodeR_ + stars;
    const float stackTop = headerBottom_ + std::max(0.0f, (avail - stack) * 0.5f);
    const float cy = stackTop + tipBand_ + gap + nodeR_ + amp;
    riverTop_ = cy - amp - nodeR_;
    auto yAt = [&](float x) { return cy + amp * std::sin(-0.5f * kPi + (x - xA) / (xB - xA) * 2.5f * kPi); };
    const float xs = xA - 76.0f * s, xe = xB + 76.0f * s;

    pts_.clear();
    arc_.clear();
    const float step = 2.0f;
    for (float x = xs; x <= xe + 0.5f * step; x += step) {
        const Vector2 p{std::min(x, xe), yAt(std::min(x, xe))};
        arc_.push_back(pts_.empty() ? 0.0f : arc_.back() + std::hypot(p.x - pts_.back().x, p.y - pts_.back().y));
        pts_.push_back(p);
    }

    nodes_.clear();
    nodeArc_.clear();
    for (int i = 0; i < app_.levelCount(); ++i) {
        const float x = xA + (xB - xA) * static_cast<float>(i) / static_cast<float>(n - 1);
        nodes_.push_back({x, yAt(x)});
        // Arc length at x: samples are evenly spaced in x.
        const float f = (x - xs) / step;
        const size_t k = std::min(arc_.size() - 2, static_cast<size_t>(f));
        nodeArc_.push_back(ease::lerp(arc_[k], arc_[k + 1], f - static_cast<float>(k)));
    }
}

Vector2 LevelSelectScene::pointAt(float arc, Vector2* tangent) const {
    arc = std::clamp(arc, 0.0f, arc_.back());
    const size_t k = std::min(arc_.size() - 2, static_cast<size_t>(std::upper_bound(arc_.begin(), arc_.end(), arc) - arc_.begin() - 1));
    const float seg = arc_[k + 1] - arc_[k];
    const float t = seg > 0.0f ? (arc - arc_[k]) / seg : 0.0f;
    const Vector2 a = pts_[k], b = pts_[k + 1];
    if (tangent) {
        const float len = std::max(1e-4f, std::hypot(b.x - a.x, b.y - a.y));
        *tangent = {(b.x - a.x) / len, (b.y - a.y) / len};
    }
    return {ease::lerp(a.x, b.x, t), ease::lerp(a.y, b.y, t)};
}

float LevelSelectScene::fillArc() const {
    const int n = app_.levelCount();
    auto arcOf = [&](int k) { return k >= n ? arc_.back() : nodeArc_[static_cast<size_t>(k)]; };
    const float a = arcOf(fillFrom_), b = arcOf(fillTo_);
    if (fillFrom_ == fillTo_) return b;
    // After a short pause, the water flows forward to the new level.
    const float duration = 0.9f + 0.35f * static_cast<float>(fillTo_ - fillFrom_);
    return ease::lerp(a, b, ease::inOutCubic((time_ - 0.55f) / duration));
}

int LevelSelectScene::nodeUnder(Vector2 p) const {
    const float hit = nodeR_ * 1.35f;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (std::hypot(p.x - nodes_[i].x, p.y - nodes_[i].y) <= hit) return static_cast<int>(i);
    return -1;
}

void LevelSelectScene::open(int index) {
    if (index >= 0 && app_.progress().unlocked(index)) app_.startLevel(index);
}

void LevelSelectScene::moveFocus(int dir) {
    const Progress& pr = app_.progress();
    for (int i = focus_ + dir; i >= 0 && i < app_.levelCount(); i += dir) {
        if (pr.unlocked(i)) {
            focus_ = i;
            return;
        }
    }
}

void LevelSelectScene::update(float dt) {
    time_ += dt;
    if (static_cast<float>(GetScreenWidth()) != layoutW_ || static_cast<float>(GetScreenHeight()) != layoutH_ ||
        ui::safeTop() != safeTop_)
        layout();

    if (input::pressed(KEY_ESCAPE)) app_.openMenu();
    const int prevHover = hover_;
    hover_ = nodeUnder(input::mouse());
    if (input::pressed(KEY_RIGHT)) { moveFocus(+1); hover_ = -1; }
    if (input::pressed(KEY_LEFT)) { moveFocus(-1); hover_ = -1; }
    // Moving onto an unlocked node with the mouse moves keyboard focus too.
    if (hover_ != prevHover && hover_ >= 0 && app_.progress().unlocked(hover_)) focus_ = hover_;
    if (input::mousePressed(MOUSE_BUTTON_LEFT) && hover_ >= 0) open(hover_);
    if (input::pressed(KEY_ENTER) || input::pressed(KEY_SPACE)) open(focus_);

    const int want = hover_ >= 0 ? hover_ : focus_;
    if (want != tipFor_) {
        tipFor_ = want;
        tipAlpha_ = 0.0f;
    }
    tipAlpha_ = ease::approach(tipAlpha_, 1.0f, 16.0f, dt);

    // Water: a conveyor of particles along the channel up to the front. Each
    // frame every particle moves forward; new ones enter at the spring. The
    // centre row runs faster (cyan), the edges slower (violet).
    waterPos_.clear();
    waterVel_.clear();
    const float k = waterScale(channelW_);
    const float fill = fillArc();
    const float spacing = kAlong * k;
    const float phase = std::fmod(time_ * 0.8f * k, spacing);
    for (int row = -1; row <= 1; ++row) {
        const float stagger = row == 0 ? 0.5f * spacing : 0.0f;
        for (float u = phase + stagger; u <= fill; u += spacing) {
            Vector2 t;
            const Vector2 c = pointAt(u, &t);
            const float off = (static_cast<float>(row) * kAcross +
                               0.014f * std::sin(u * 0.09f + time_ * 2.6f + static_cast<float>(row) * 1.7f)) * k;
            waterPos_.push_back({(c.x - t.y * off) / k, (c.y + t.x * off) / k});
            const float speed = row == 0 ? 2.8f : 1.3f;
            waterVel_.push_back({t.x * speed, t.y * speed});
        }
    }
}

void LevelSelectScene::prepare() {
    // Thumbnails are drawn by the real pipeline into their own targets: the
    // one the tooltip needs now, plus one more per frame until all exist.
    if (tipFor_ >= 0 && tipFor_ < app_.levelCount()) app_.levelThumbnail(tipFor_);
    for (int i = 0; i < app_.levelCount(); ++i) {
        if (!app_.hasLevelThumbnail(i)) {
            app_.levelThumbnail(i);
            break;
        }
    }
}

void LevelSelectScene::drawWorld() {
    if (pts_.size() < 2) return;
    const float s = ui::scale();
    const int n = app_.levelCount();
    const Progress& pr = app_.progress();
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.2f);

    // A quiet violet haze low on the screen.
    const Texture2D& blob = assets().blob;
    DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                   {layoutW_ * 0.05f, layoutH_ * 0.5f, layoutW_ * 0.9f, layoutH_ * 0.9f}, {0, 0}, 0.0f,
                   ease::alpha(theme::violet, 0.08f));

    // The carved channel: a soft outer glow, a lavender rim and a dark bed.
    Renderer::blendAdditive();
    for (size_t i = 0; i < pts_.size(); i += 5) glowAt(pts_[i], channelW_ * 1.25f, ease::alpha(theme::violet, 0.018f));
    Renderer::blendNormal();
    const Color rim = ease::mix(theme::bgTop, theme::rimLight, 0.30f);
    const Color bed{9, 8, 22, 255};
    stroke(pts_, pts_.size(), channelW_ + 3.0f * s, rim);
    stroke(pts_, pts_.size(), channelW_, bed);
    // A darker inner line gives the bed some depth.
    stroke(pts_, pts_.size(), channelW_ * 0.45f, Color{6, 5, 15, 255});
    // The pool at the end.
    const Vector2 end = pts_.back();
    DrawCircleV(end, channelW_ * 0.95f + 1.5f * s, rim);
    DrawCircleV(end, channelW_ * 0.95f, bed);

    // Glows under the nodes (in the world layer, so they bloom).
    const float front = fillArc();
    Renderer::blendAdditive();
    glowAt(pts_.front(), channelW_ * 1.6f, ease::alpha(theme::cyan, 0.35f + 0.1f * pulse)); // the spring
    for (int i = 0; i < n; ++i) {
        const float reached = ease::clamp01((front - nodeArc_[static_cast<size_t>(i)]) / (10.0f * s) + 1.0f);
        if (pr.beaten(i)) glowAt(nodes_[static_cast<size_t>(i)], nodeR_ * 2.4f, ease::alpha(theme::violet, 0.28f * reached));
        else if (pr.unlocked(i)) glowAt(nodes_[static_cast<size_t>(i)], nodeR_ * (2.6f + 0.3f * pulse), ease::alpha(theme::cyan, (0.22f + 0.12f * pulse) * reached));
    }
    if (fillTo_ >= n && n > 0) glowAt(end, channelW_ * 2.4f, ease::alpha(theme::cyan, 0.3f * ease::clamp01((front - arc_.back()) / (10.0f * s) + 1.0f)));
    Renderer::blendNormal();

    View v;
    v.scale = waterScale(channelW_);
    v.offset = {0.0f, 0.0f};
    app_.renderer().drawWater(waterPos_, waterVel_, v);
}

void LevelSelectScene::drawNode(int i, float appear) {
    const float s = ui::scale();
    const Progress& pr = app_.progress();
    const bool beaten = pr.beaten(i), unlocked = pr.unlocked(i);
    const Vector2 c = nodes_[static_cast<size_t>(i)];
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.2f);
    const float hot = ui::animate("node:" + std::to_string(i), (i == focus_ || i == hover_) && unlocked ? 1.0f : 0.0f, 12.0f);
    // The current level lights up when the water reaches it.
    const float reached = ease::clamp01((fillArc() - nodeArc_[static_cast<size_t>(i)]) / (10.0f * s) + 1.0f);
    const float r = nodeR_ * (0.6f + 0.4f * ease::outBack(appear)) * (1.0f + 0.08f * hot);
    const float a = ease::clamp01(appear * 1.5f);

    if (beaten) {
        DrawCircleV(c, r, ease::alpha(ease::mix(theme::violet, Color{30, 20, 70, 255}, 0.35f), a));
        DrawRing(c, r - 1.5f * s, r, 0.0f, 360.0f, 48, ease::alpha(theme::rimLight, 0.75f * a));
    } else if (unlocked) {
        const Color lit = ease::mix(theme::violet, WHITE, 0.18f);
        DrawCircleV(c, r, ease::alpha(ease::mix(Color{24, 20, 52, 255}, lit, reached), a));
        const float ringR = r + (4.0f + 3.0f * pulse) * s;
        DrawRing(c, ringR - 2.0f * s, ringR, 0.0f, 360.0f, 64, ease::alpha(theme::cyan, (0.45f + 0.45f * reached) * a));
    } else {
        DrawCircleV(c, r, ease::alpha(Color{17, 15, 36, 255}, a));
        DrawRing(c, r - 1.2f * s, r, 0.0f, 360.0f, 48, ease::alpha(theme::textMuted, 0.45f * a));
        padlock({c.x, c.y + 1.0f * s}, 13.0f * s, ease::alpha(theme::textDim, 0.75f * a));
    }
    if (hot > 0.01f) {
        const float fr = r + (beaten ? 5.0f : 10.0f) * s;
        DrawRing(c, fr - 1.2f * s, fr, 0.0f, 360.0f, 64, ease::alpha(WHITE, 0.55f * hot * a));
    }
    if (unlocked) {
        const std::string num = std::to_string(i + 1);
        const float ns = theme::type::body * s;
        const Vector2 m = measureText(FontRole::SemiBold, num, ns);
        drawText(FontRole::SemiBold, num, {c.x - m.x * 0.5f, c.y - m.y * 0.5f}, ns, ease::alpha(theme::text, a));
    }
    if (beaten) {
        // Stars earned, under the node.
        const int stars = pr.levels[static_cast<size_t>(i)].stars;
        const float sr = 5.5f * s, gap = 13.5f * s, sy = c.y + nodeR_ + 13.0f * s;
        for (int st = 0; st < 3; ++st)
            ui::star({c.x + (static_cast<float>(st) - 1.0f) * gap, sy}, sr, st < stars ? 1.0f : 0.0f, a);
    }
}

void LevelSelectScene::drawTooltip(int i) {
    const float s = ui::scale();
    const Progress& pr = app_.progress();
    const LevelDesc& lv = app_.level(i);
    const bool unlocked = pr.unlocked(i), beaten = pr.beaten(i);
    const float a = tipAlpha_ * ease::clamp01((time_ - 0.25f) / 0.35f);
    if (a <= 0.01f) return;

    const TipMetrics m = tipMetrics(i);
    const float w = m.w, h = m.h, pad = m.pad, thumbW = m.thumbW, thumbH = m.thumbH;
    const float capS = m.capS, nameS = m.nameS, hintS = m.hintS, lineH = m.lineH, statsH = m.statsH;
    const Vector2 capM = m.capM, nameM = m.nameM;
    const std::vector<std::string>& hint = m.hint;
    const std::string cap = fmt("LEVEL %02d", i + 1);

    // In the band above the river, centred over the node, kept on screen.
    const float margin = 14.0f * s;
    const float x = std::clamp(nodes_[static_cast<size_t>(i)].x - w * 0.5f, margin, layoutW_ - margin - w);
    const float y = riverTop_ - 14.0f * s - h + (1.0f - a) * 6.0f * s;

    const Rectangle card{x, y, w, h};
    ui::glass(card, 18.0f * s, a);
    const Rectangle thumb{x + pad, y + pad, thumbW, thumbH};
    if (app_.hasLevelThumbnail(i)) {
        const Texture2D& t = app_.levelThumbnail(i);
        DrawTexturePro(t, {0, 0, static_cast<float>(t.width), -static_cast<float>(t.height)}, thumb, {0, 0}, 0.0f,
                       ease::alpha(WHITE, a));
    } else {
        DrawRectangleRec(thumb, ease::alpha(theme::bgBottom, a));
    }
    if (!unlocked) {
        DrawRectangleRec(thumb, ease::alpha(Color{8, 6, 20, 255}, 0.6f * a));
        padlock({thumb.x + thumb.width * 0.5f, thumb.y + thumb.height * 0.5f}, 26.0f * s, ease::alpha(theme::textDim, a));
    }
    DrawRectangleLinesEx(thumb, 1.0f, ease::alpha(Color{255, 255, 255, 34}, a));

    float ty = thumb.y + thumb.height + 12.0f * s;
    drawText(FontRole::Medium, cap, {x + pad, ty}, capS, ease::alpha(unlocked ? theme::cyan : theme::textDim, a), 0.14f);
    ty += capM.y + 3.0f * s;
    drawText(FontRole::SemiBold, lv.name, {x + pad, ty}, nameS, ease::alpha(unlocked ? theme::text : theme::textDim, a));
    ty += nameM.y + 6.0f * s;
    for (const std::string& line : hint) {
        drawText(FontRole::Regular, line, {x + pad, ty}, hintS, ease::alpha(theme::textDim, a));
        ty += lineH;
    }
    ty += 10.0f * s;

    const float statS = theme::type::small * s;
    if (beaten) {
        const LevelRecord rec = pr.levels[static_cast<size_t>(i)];
        const float sr = 8.0f * s;
        for (int st = 0; st < 3; ++st)
            ui::star({x + pad + sr + static_cast<float>(st) * (2.0f * sr + 6.0f * s), ty + statsH * 0.5f}, sr,
                     st < rec.stars ? 1.0f : 0.0f, a);
        const std::string bestText = fmt("Best %d%%", rec.bestPercent);
        const Vector2 bm = measureText(FontRole::Medium, bestText, statS);
        drawText(FontRole::Medium, bestText, {x + w - pad - bm.x, ty + (statsH - bm.y) * 0.5f}, statS, ease::alpha(theme::text, a));
    } else {
        const std::string msg = unlocked ? std::string("Not beaten yet") : fmt("Beat level %d to unlock", i);
        const Vector2 mm = measureText(FontRole::Regular, msg, statS);
        drawText(FontRole::Regular, msg, {x + pad, ty + (statsH - mm.y) * 0.5f}, statS, ease::alpha(theme::textDim, a));
        if (unlocked) {
            const std::string go = "Enter to play";
            const Vector2 gm = measureText(FontRole::Medium, go, statS);
            drawText(FontRole::Medium, go, {x + w - pad - gm.x, ty + (statsH - gm.y) * 0.5f}, statS, ease::alpha(theme::cyan, a));
        }
    }
}

void LevelSelectScene::drawUi() {
    const float s = ui::scale();
    const Progress& pr = app_.progress();
    const float in = ease::outCubic(std::min(1.0f, time_ / 0.5f));

    // Header: title on the left, total stars on the right.
    const float x = 64.0f * s, y = safeTop_ + 44.0f * s;
    drawText(FontRole::SemiBold, "Levels", {x, y}, theme::type::title * 1.2f * s, ease::alpha(theme::text, in));
    const float titleH = measureText(FontRole::SemiBold, "Levels", theme::type::title * 1.2f * s).y;
    drawText(FontRole::Regular, "Esc to go back", {x + 2.0f * s, y + titleH + 2.0f * s}, theme::type::small * s,
             ease::alpha(theme::textDim, in));
    const std::string total = std::to_string(pr.totalStars()) + " / " + std::to_string(3 * app_.levelCount());
    const float ts = theme::type::heading * s;
    const Vector2 tm = measureText(FontRole::SemiBold, total, ts);
    const float starR = 13.0f * s;
    const float tx = layoutW_ - x - tm.x;
    const float tyMid = y + titleH * 0.5f;
    drawText(FontRole::SemiBold, total, {tx, tyMid - tm.y * 0.5f}, ts, ease::alpha(theme::text, in));
    ui::star({tx - starR - 12.0f * s, tyMid}, starR, 1.0f, in);

    // Footer: controls.
    const std::string help = "Left / Right to choose    Enter to play";
    const float hs = theme::type::small * s;
    const Vector2 hm = measureText(FontRole::Regular, help, hs);
    drawText(FontRole::Regular, help, {(layoutW_ - hm.x) * 0.5f, layoutH_ - 32.0f * s - hm.y * 0.5f}, hs,
             ease::alpha(theme::textMuted, in));

    for (int i = 0; i < app_.levelCount(); ++i) {
        const float appear = ease::clamp01((time_ - 0.05f - 0.04f * static_cast<float>(i)) / 0.4f);
        drawNode(i, appear);
    }
    if (tipFor_ >= 0 && tipFor_ < app_.levelCount()) drawTooltip(tipFor_);
}
