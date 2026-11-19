#pragma once

// Player progress: best result per level and the unlock rule.
// Pure data + JSON (de)serialisation, so it is unit-tested; the game decides
// where the file lives.

#include <string>
#include <string_view>
#include <vector>

namespace spillway {

struct LevelRecord {
    int stars = 0;       // best stars, 0 = not beaten
    int bestPercent = 0; // best % saved in a won run
};

struct Progress {
    std::vector<LevelRecord> levels;

    void ensureSize(int count) { if (static_cast<int>(levels.size()) < count) levels.resize(static_cast<size_t>(count)); }
    bool beaten(int i) const { return i >= 0 && i < static_cast<int>(levels.size()) && levels[static_cast<size_t>(i)].stars > 0; }
    // Level 0 is always open; level N+1 opens when level N is beaten.
    bool unlocked(int i) const { return i == 0 || beaten(i - 1); }
    // First level not yet beaten (or the last level if all are).
    int firstUnbeaten(int count) const;
    bool anyProgress() const;
    int totalStars() const;

    // Records a won run. Returns true if it beat the previous best stars or
    // percentage (a "New best!").
    bool record(int level, int stars, int percent);
};

std::string progressToJson(const Progress& p);
// Returns false (and leaves `out` empty) if the text is not a valid save.
bool progressFromJson(std::string_view text, Progress& out);

} // namespace spillway
