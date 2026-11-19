#include "spillway/progress.h"

#include <algorithm>

#include "spillway/json.h"

namespace spillway {

int Progress::firstUnbeaten(int count) const {
    for (int i = 0; i < count; ++i)
        if (!beaten(i)) return i;
    return std::max(0, count - 1);
}

bool Progress::anyProgress() const {
    return std::any_of(levels.begin(), levels.end(), [](const LevelRecord& r) { return r.stars > 0; });
}

int Progress::totalStars() const {
    int t = 0;
    for (const LevelRecord& r : levels) t += r.stars;
    return t;
}

bool Progress::record(int level, int stars, int percent) {
    if (level < 0 || stars <= 0) return false;
    ensureSize(level + 1);
    LevelRecord& r = levels[static_cast<size_t>(level)];
    const bool better = stars > r.stars || percent > r.bestPercent;
    r.stars = std::max(r.stars, stars);
    r.bestPercent = std::max(r.bestPercent, percent);
    return better;
}

std::string progressToJson(const Progress& p) {
    json::Array levels;
    for (const LevelRecord& r : p.levels) levels.push_back(json::Object{{"stars", r.stars}, {"best", r.bestPercent}});
    return json::dump(json::Object{{"version", 1}, {"levels", std::move(levels)}});
}

bool progressFromJson(std::string_view text, Progress& out) {
    out = {};
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return false;
    const json::Array* levels = (*v)["levels"].array();
    if (!levels) return false;
    for (const json::Value& l : *levels) {
        LevelRecord r;
        r.stars = std::clamp(static_cast<int>(l["stars"].number(0)), 0, 3);
        r.bestPercent = std::clamp(static_cast<int>(l["best"].number(0)), 0, 100);
        out.levels.push_back(r);
    }
    return true;
}

} // namespace spillway
