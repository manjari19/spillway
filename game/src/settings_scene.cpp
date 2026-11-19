#include "settings_scene.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "app.h"
#include "assets.h"
#include "ease.h"
#include "input.h"
#include "theme.h"
#include "widgets.h"

namespace {

// An iOS-style switch whose knob slides with an eased value.
void toggle(const std::string& id, Rectangle r, bool on, float alpha) {
    const float t = ui::animate("tg:" + id, on ? 1.0f : 0.0f, 14.0f);
    const Color track = ease::mix(Color{255, 255, 255, 26}, Color{109, 60, 255, 230}, t);
    DrawRectangleRounded(r, 1.0f, 24, ease::alpha(track, alpha));
    if (t > 0.01f) ui::glow(r, r.height * 0.5f, 10.0f * ui::scale(), ease::alpha(theme::violet, 0.35f * t * alpha), 0.0f);
    const float kr = r.height * 0.5f - 3.0f * ui::scale();
    const float kx = ease::lerp(r.x + r.height * 0.5f, r.x + r.width - r.height * 0.5f, t);
    DrawCircleV({kx, r.y + r.height * 0.5f}, kr, ease::alpha(theme::text, alpha));
}

} // namespace

SettingsScene::SettingsScene(App& app) : app_(app) {}

void SettingsScene::change(int row, int dir) {
    Settings& st = app_.settings();
    switch (row) {
    case Bloom: st.bloom = !st.bloom; break;
    case Quality: st.quality = std::clamp(st.quality + dir, 0, 2); break;
    case Fps: st.showFps = !st.showFps; break;
    case Fullscreen: st.fullscreen = !st.fullscreen; break;
    case Reset:
        if (!confirmReset_) { confirmReset_ = true; confirmSince_ = time_; return; }
        break;
    case Back: app_.openMenu(); return;
    }
    if (row != Reset) app_.applySettings();
}

void SettingsScene::update(float dt) {
    time_ += dt;
    if (input::pressed(KEY_ESCAPE)) {
        if (confirmReset_) confirmReset_ = false;
        else app_.openMenu();
    }
    if (!confirmReset_) {
        if (input::pressed(KEY_DOWN)) focus_ = (focus_ + 1) % RowCount;
        if (input::pressed(KEY_UP)) focus_ = (focus_ + RowCount - 1) % RowCount;
        if (input::pressed(KEY_LEFT) && focus_ == Quality) change(Quality, -1);
        if (input::pressed(KEY_RIGHT) && focus_ == Quality) change(Quality, +1);
        if ((input::pressed(KEY_ENTER) || input::pressed(KEY_SPACE)) && focus_ != Back) {
            if (focus_ == Quality) {
                Settings& st = app_.settings();
                st.quality = (st.quality + 1) % 3;
                app_.applySettings();
            } else {
                change(focus_, 0);
            }
        }
    }
}

void SettingsScene::drawWorld() {
    const Texture2D& blob = assets().blob;
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    DrawTexturePro(blob, {0, 0, static_cast<float>(blob.width), static_cast<float>(blob.height)},
                   {sw * 0.15f, sh * 0.5f, sw * 0.7f, sh * 0.9f}, {0, 0}, 0.0f, ease::alpha(theme::violet, 0.10f));
}

void SettingsScene::drawUi() {
    const float s = ui::scale();
    const float in = ease::outCubic(std::min(1.0f, time_ / 0.4f));
    Settings& st = app_.settings();

    const float pad = 34.0f * s, rowH = 58.0f * s;
    const float w = 560.0f * s;
    const Vector2 tM = measureText(FontRole::SemiBold, "Settings", theme::type::title * s);
    const float h = pad + tM.y + 22.0f * s + 5 * rowH + 16.0f * s + 48.0f * s + pad;
    const Rectangle card{(static_cast<float>(GetScreenWidth()) - w) * 0.5f,
                         (static_cast<float>(GetScreenHeight()) - h) * 0.5f + (1 - in) * 10.0f * s, w, h};
    ui::glass(card, 28.0f * s, in);
    float y = card.y + pad;
    drawText(FontRole::SemiBold, "Settings", {card.x + pad, y}, theme::type::title * s, ease::alpha(theme::text, in));
    y += tM.y + 22.0f * s;

    const char* labels[] = {"Bloom", "Water quality", "Show FPS", "Fullscreen", "Reset progress"};
    const char* hints[] = {"Glow around water and walls", "Particle count; applies to the next level you start",
                           "Frame counter in the corner", "Borderless fullscreen window", "Clears all stars and unlocks"};
    const Vector2 mouse = input::mouse();
    for (int i = 0; i < 5; ++i) {
        const Rectangle row{card.x + pad * 0.5f, y, card.width - pad, rowH - 6.0f * s};
        const bool hovered = CheckCollisionPointRec(mouse, row);
        if (hovered && !confirmReset_) focus_ = i;
        const float hf = ui::animate("row:" + std::to_string(i), focus_ == i ? 1.0f : 0.0f, 14.0f);
        if (hf > 0.01f) DrawRectangleRounded(row, ui::roundness(row, 14.0f * s), 16, ease::alpha(Color{255, 255, 255, 14}, hf * in));
        const float ls = theme::type::body * s, hs = theme::type::caption * s;
        const Vector2 lm = measureText(FontRole::Medium, labels[i], ls);
        const Vector2 hm = measureText(FontRole::Regular, hints[i], hs);
        const float ty = row.y + (row.height - (lm.y + 2.0f * s + hm.y)) * 0.5f;
        drawText(FontRole::Medium, labels[i], {row.x + pad * 0.5f, ty}, ls, ease::alpha(theme::text, in));
        drawText(FontRole::Regular, hints[i], {row.x + pad * 0.5f, ty + lm.y + 2.0f * s}, hs, ease::alpha(theme::textDim, in));

        const float rx = row.x + row.width - pad * 0.5f;
        const float cy = row.y + row.height * 0.5f;
        const bool click = hovered && input::mousePressed(MOUSE_BUTTON_LEFT) && !confirmReset_;
        if (i == Bloom || i == Fps || i == Fullscreen) {
            const bool on = i == Bloom ? st.bloom : i == Fps ? st.showFps : st.fullscreen;
            toggle(labels[i], {rx - 52.0f * s, cy - 15.0f * s, 52.0f * s, 30.0f * s}, on, in);
            if (click) change(i, 0);
        } else if (i == Quality) {
            // Segmented control with a sliding highlight.
            const char* opts[] = {"Low", "Medium", "High"};
            const float segW = 76.0f * s, segH = 32.0f * s;
            const Rectangle seg{rx - 3 * segW, cy - segH * 0.5f, 3 * segW, segH};
            DrawRectangleRounded(seg, 1.0f, 24, ease::alpha(Color{255, 255, 255, 18}, in));
            const float sel = ui::animate("seg", static_cast<float>(st.quality), 14.0f);
            const Rectangle hl{seg.x + sel * segW + 3.0f * s, seg.y + 3.0f * s, segW - 6.0f * s, segH - 6.0f * s};
            DrawRectangleRounded(hl, 1.0f, 24, ease::alpha(Color{109, 60, 255, 230}, in));
            for (int o = 0; o < 3; ++o) {
                const Rectangle cell{seg.x + o * segW, seg.y, segW, segH};
                ui::textCentered(FontRole::Medium, opts[o], cell, theme::type::small * s,
                                 ease::alpha(o == st.quality ? theme::text : theme::textDim, in));
                if (click && CheckCollisionPointRec(mouse, cell)) {
                    st.quality = o;
                    app_.applySettings();
                }
            }
        } else if (i == Reset) {
            if (!confirmReset_) {
                const auto b = ui::button("set:reset", {rx - 120.0f * s, cy - 18.0f * s, 120.0f * s, 36.0f * s},
                                          resetDone_ ? "Done" : "Reset", false, false, in);
                if (b.clicked) { confirmReset_ = true; confirmSince_ = time_; }
            } else {
                // Confirm step: two buttons replace the one.
                const bool enter = input::pressed(KEY_ENTER) && time_ > confirmSince_ + 0.05f;
                const auto yes = ui::button("set:yes", {rx - 120.0f * s, cy - 18.0f * s, 120.0f * s, 36.0f * s}, "Reset all",
                                            true, enter, in, true);
                const auto no = ui::button("set:no", {rx - 250.0f * s, cy - 18.0f * s, 120.0f * s, 36.0f * s}, "Cancel",
                                           false, false, in);
                if (yes.clicked) {
                    app_.resetProgress();
                    confirmReset_ = false;
                    resetDone_ = true;
                }
                if (no.clicked) confirmReset_ = false;
            }
        }
        y += rowH;
    }
    y += 16.0f * s;
    const auto back = ui::button("set:back", {card.x + pad, y, card.width - 2 * pad, 48.0f * s}, "Back", focus_ == Back,
                                 !confirmReset_ && focus_ == Back && input::pressed(KEY_ENTER), in);
    if (back.hovered && !confirmReset_) focus_ = Back;
    if (back.clicked) app_.openMenu();
}
