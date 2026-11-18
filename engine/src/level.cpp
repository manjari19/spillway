#include "spillway/level.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace spillway {

bool Shape::contains(Vec2 p) const {
    switch (kind) {
    case Kind::Rect:
        return p.x >= points[0].x && p.x < points[1].x && p.y >= points[0].y && p.y < points[1].y;
    case Kind::Circle:
        return lengthSq(p - points[0]) <= radius * radius;
    case Kind::Polygon: {
        // Even-odd rule: cast a ray to the right and count edge crossings.
        bool inside = false;
        const size_t n = points.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = points[i], b = points[j];
            if ((a.y > p.y) != (b.y > p.y)) {
                const float xCross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (p.x < xCross) inside = !inside;
            }
        }
        return inside;
    }
    }
    return false;
}

namespace {

std::string_view trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitWords(std::string_view s) {
    std::vector<std::string> out;
    std::istringstream in{std::string(s)};
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

// Parses the whole token as a finite float.
bool parseFloat(const std::string& token, float& out) {
    char* end = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

struct Parser {
    int line = 0;
    std::string error;

    bool fail(const std::string& msg) {
        if (error.empty()) error = "line " + std::to_string(line) + ": " + msg;
        return false;
    }

    // Reads words[from..] as exactly `count` numbers (count < 0: any number).
    bool numbers(const std::vector<std::string>& words, size_t from, int count, std::vector<float>& out,
                 const std::string& what) {
        out.clear();
        for (size_t i = from; i < words.size(); ++i) {
            float v;
            if (!parseFloat(words[i], v)) return fail("expected a number in " + what + ", got '" + words[i] + "'");
            out.push_back(v);
        }
        if (count >= 0 && static_cast<int>(out.size()) != count)
            return fail(what + " needs " + std::to_string(count) + " numbers, got " + std::to_string(out.size()));
        return true;
    }

    bool rect(const std::vector<std::string>& words, size_t from, Rect& r, const std::string& what) {
        if (words.size() <= from || words[from] != "rect") return fail(what + " must be a rect: '" + what + " rect x0 y0 x1 y1'");
        std::vector<float> v;
        if (!numbers(words, from + 1, 4, v, what + " rect")) return false;
        if (v[2] <= v[0] || v[3] <= v[1]) return fail(what + " rect needs x1 > x0 and y1 > y0");
        r = {{v[0], v[1]}, {v[2], v[3]}};
        return true;
    }

    bool shape(const std::vector<std::string>& words, size_t from, Shape& s, const std::string& what) {
        if (words.size() <= from) return fail(what + " needs a shape (rect, circle or poly)");
        const std::string& kind = words[from];
        std::vector<float> v;
        if (kind == "rect") {
            Rect r;
            if (!rect(words, from, r, what)) return false;
            s = {Shape::Kind::Rect, {r.min, r.max}, 0.0f};
        } else if (kind == "circle") {
            if (!numbers(words, from + 1, 3, v, what + " circle")) return false;
            if (v[2] <= 0.0f) return fail("circle radius must be positive");
            s = {Shape::Kind::Circle, {{v[0], v[1]}}, v[2]};
        } else if (kind == "poly") {
            if (!numbers(words, from + 1, -1, v, what + " poly")) return false;
            if (v.size() < 6 || v.size() % 2 != 0) return fail("poly needs at least 3 x,y pairs");
            s = {Shape::Kind::Polygon, {}, 0.0f};
            for (size_t i = 0; i < v.size(); i += 2) s.points.push_back({v[i], v[i + 1]});
        } else {
            return fail("unknown shape '" + kind + "' (expected rect, circle or poly)");
        }
        return true;
    }
};

bool rectInside(const Rect& r, Vec2 size) {
    return r.min.x >= 0.0f && r.min.y >= 0.0f && r.max.x <= size.x && r.max.y <= size.y;
}

} // namespace

LevelLoadResult parseLevel(std::string_view text, bool requireGoals) {
    LevelDesc level;
    Parser p;
    bool haveSize = false, haveWin = false;

    size_t pos = 0;
    while (pos <= text.size() && p.error.empty()) {
        const size_t nl = text.find('\n', pos);
        std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++p.line;

        if (const auto hash = raw.find('#'); hash != std::string_view::npos) raw = raw.substr(0, hash);
        const std::string_view line = trim(raw);
        if (line.empty()) continue;

        const auto words = splitWords(line);
        const std::string& cmd = words[0];
        std::vector<float> v;

        if (cmd == "name" || cmd == "hint") {
            const std::string rest{trim(line.substr(cmd.size()))};
            if (rest.empty()) { p.fail(cmd + " needs text"); break; }
            (cmd == "name" ? level.name : level.hint) = rest;
        } else if (cmd == "size") {
            if (!p.numbers(words, 1, 2, v, "size")) break;
            if (v[0] <= 0.0f || v[1] <= 0.0f || v[0] > 100.0f || v[1] > 100.0f) { p.fail("size must be between 0 and 100 m"); break; }
            level.size = {v[0], v[1]};
            haveSize = true;
        } else if (cmd == "cell") {
            if (!p.numbers(words, 1, 1, v, "cell")) break;
            if (v[0] < 0.02f || v[0] > 0.5f) { p.fail("cell size must be between 0.02 and 0.5 m"); break; }
            level.cellSize = v[0];
        } else if (cmd == "win") {
            if (!p.numbers(words, 1, 1, v, "win")) break;
            if (v[0] <= 0.0f || v[0] > 100.0f) { p.fail("win must be a percentage in (0, 100]"); break; }
            level.winPercent = v[0];
            haveWin = true;
        } else if (cmd == "ink") {
            if (!p.numbers(words, 1, 1, v, "ink")) break;
            if (v[0] < 0.0f) { p.fail("ink cannot be negative"); break; }
            level.inkMetres = v[0];
        } else if (cmd == "stars") {
            if (!p.numbers(words, 1, 2, v, "stars")) break;
            if (v[0] <= 0.0f || v[1] > 100.0f || v[1] < v[0]) { p.fail("stars needs 0 < two-star <= three-star <= 100"); break; }
            level.twoStarPercent = v[0];
            level.threeStarPercent = v[1];
        } else if (cmd == "empty" || cmd == "dirt" || cmd == "rock") {
            TerrainOp op;
            op.material = cmd == "empty" ? Cell::Empty : cmd == "dirt" ? Cell::Dirt : Cell::Rock;
            if (!p.shape(words, 1, op.shape, cmd)) break;
            level.terrain.push_back(std::move(op));
        } else if (cmd == "water" || cmd == "drain" || cmd == "pit") {
            Rect r;
            if (!p.rect(words, 1, r, cmd)) break;
            if (haveSize && !rectInside(r, level.size)) { p.fail(cmd + " rect lies outside the level"); break; }
            (cmd == "water" ? level.water : cmd == "drain" ? level.drains : level.pits).push_back(r);
        } else if (cmd == "wall") {
            p.fail("walls are drawn by the player and cannot be placed in a level file");
        } else {
            p.fail("unknown command '" + cmd + "'");
        }
    }

    if (p.error.empty()) {
        if (!haveSize) p.error = "missing required 'size'";
        else if (!haveWin) p.error = "missing required 'win'";
        else if (requireGoals && level.water.empty()) p.error = "level has no 'water'";
        else if (requireGoals && level.drains.empty()) p.error = "level has no 'drain'";
        else if (level.twoStarPercent >= 0.0f && level.twoStarPercent < level.winPercent)
            p.error = "two-star percentage cannot be below the win percentage";
        for (const auto* list : {&level.water, &level.drains, &level.pits})
            for (const Rect& r : *list)
                if (p.error.empty() && !rectInside(r, level.size)) p.error = "a water/drain/pit rect lies outside the level";
    }
    if (!p.error.empty()) return {std::nullopt, p.error};
    // Default star thresholds: 40% and 70% of the way from win to 100%.
    if (level.twoStarPercent < 0.0f) {
        level.twoStarPercent = level.winPercent + 0.4f * (100.0f - level.winPercent);
        level.threeStarPercent = level.winPercent + 0.7f * (100.0f - level.winPercent);
    }
    return {std::move(level), {}};
}

LevelLoadResult loadLevelFile(const std::string& path, bool requireGoals) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {std::nullopt, "cannot open level file '" + path + "'"};
    std::ostringstream ss;
    ss << file.rdbuf();
    LevelLoadResult result = parseLevel(ss.str(), requireGoals);
    if (!result) result.error = path + ": " + result.error;
    return result;
}

Terrain buildTerrain(const LevelDesc& level) {
    const int w = static_cast<int>(std::ceil(level.size.x / level.cellSize - 1e-4f));
    const int h = static_cast<int>(std::ceil(level.size.y / level.cellSize - 1e-4f));
    Terrain terrain(w, h, level.cellSize);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Vec2 c = terrain.cellCentre(x, y);
            for (auto it = level.terrain.rbegin(); it != level.terrain.rend(); ++it) {
                if (it->shape.contains(c)) { terrain.set(x, y, it->material); break; }
            }
        }
    }
    terrain.rebuildSdf();
    return terrain;
}

} // namespace spillway
