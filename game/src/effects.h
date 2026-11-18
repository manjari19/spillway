#pragma once

// Tiny cosmetic particles: splash droplets at drains, dust when digging.
// Purely visual; they do not touch the simulation.

#include <raylib.h>

#include <vector>

#include "spillway/rng.h"
#include "spillway/vec2.h"
#include "view.h"

class Effects {
public:
    void splash(spillway::Vec2 at);   // water reaching a drain
    void dust(spillway::Vec2 at);     // a dug cell
    void sink(spillway::Vec2 at);     // water lost in a pit
    void update(float dt);
    void draw(const View& view) const;
    void clear() { bits_.clear(); }

private:
    struct Bit {
        spillway::Vec2 p, v;
        float life, maxLife, size;
        Color color;
        float gravity;
        bool additive;
    };
    std::vector<Bit> bits_;
    spillway::Rng rng_{7};
};
