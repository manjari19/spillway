#include "effects.h"

#include <algorithm>
#include <cmath>

#include "assets.h"
#include "ease.h"
#include "renderer.h"
#include "theme.h"

using spillway::Vec2;

namespace {
constexpr size_t kMaxBits = 1500;
}

void Effects::splash(Vec2 at) {
    if (bits_.size() >= kMaxBits || rng_.uniform() > 0.6f) return;
    const int n = 1 + static_cast<int>(rng_.uniform() * 2.0f);
    for (int i = 0; i < n; ++i) {
        const float ang = -1.5708f + rng_.uniform(-0.9f, 0.9f);
        const float sp = rng_.uniform(1.2f, 3.0f);
        bits_.push_back({at, {std::cos(ang) * sp, std::sin(ang) * sp}, 0.0f, rng_.uniform(0.35f, 0.6f),
                         rng_.uniform(0.025f, 0.045f), ease::mix(theme::waterFast, theme::wall, rng_.uniform() * 0.5f),
                         9.0f, true});
    }
}

void Effects::sink(Vec2 at) {
    if (bits_.size() >= kMaxBits || rng_.uniform() > 0.4f) return;
    bits_.push_back({at, {rng_.uniform(-0.3f, 0.3f), rng_.uniform(-0.8f, -0.3f)}, 0.0f, 0.7f, 0.06f, theme::danger,
                     -0.5f, true});
}

void Effects::dust(Vec2 at) {
    if (bits_.size() >= kMaxBits || rng_.uniform() > 0.12f) return;
    bits_.push_back({at + Vec2{rng_.uniform(-0.05f, 0.05f), rng_.uniform(-0.05f, 0.05f)},
                     {rng_.uniform(-0.25f, 0.25f), rng_.uniform(-0.5f, -0.1f)}, 0.0f, rng_.uniform(0.5f, 0.9f),
                     rng_.uniform(0.08f, 0.16f), ease::mix(theme::dirtTop, Color{150, 120, 140, 255}, rng_.uniform()),
                     0.6f, false});
}

void Effects::update(float dt) {
    for (Bit& b : bits_) {
        b.life += dt;
        b.v.y += b.gravity * dt;
        b.v *= std::exp(-1.5f * dt);
        b.p += b.v * dt;
    }
    std::erase_if(bits_, [](const Bit& b) { return b.life >= b.maxLife; });
}

void Effects::draw(const View& view) const {
    const Texture2D& tex = assets().blob;
    const Rectangle src{0, 0, static_cast<float>(tex.width), static_cast<float>(tex.height)};
    for (int pass = 0; pass < 2; ++pass) {
        const bool additive = pass == 1;
        if (additive) Renderer::blendAdditive();
        for (const Bit& b : bits_) {
            if (b.additive != additive) continue;
            const float t = b.life / b.maxLife;
            // Fade in fast, out slowly; dust also grows as it spreads.
            const float a = std::min(1.0f, t * 8.0f) * (1.0f - ease::outCubic(t));
            const float r = b.size * view.scale * (additive ? 1.0f : 0.7f + 0.8f * t) * 2.0f;
            const Vector2 c = view.toScreen(b.p);
            DrawTexturePro(tex, src, {c.x - r, c.y - r, 2 * r, 2 * r}, {0, 0}, 0.0f,
                           ease::alpha(b.color, additive ? a : a * 0.35f));
        }
        if (additive) Renderer::blendNormal();
    }
}
