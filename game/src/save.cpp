#include "save.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <algorithm>

#include "spillway/json.h"

namespace fs = std::filesystem;
using namespace spillway;

std::string saveDirectory() {
    fs::path dir;
    if (const char* o = std::getenv("SPILLWAY_SAVE_DIR")) {
        dir = o;
    } else {
#if defined(_WIN32)
        if (const char* a = std::getenv("APPDATA")) dir = fs::path(a) / "Spillway";
#elif defined(__APPLE__)
        if (const char* h = std::getenv("HOME")) dir = fs::path(h) / "Library" / "Application Support" / "Spillway";
#else
        if (const char* x = std::getenv("XDG_DATA_HOME")) dir = fs::path(x) / "Spillway";
        else if (const char* h = std::getenv("HOME")) dir = fs::path(h) / ".local" / "share" / "Spillway";
#endif
    }
    if (dir.empty()) return {};
    std::error_code ec;
    fs::create_directories(dir, ec);
    return ec ? std::string{} : dir.string();
}

namespace {

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Write to a temp file and rename over the old one, so a crash mid-write
// can never leave a half-written save behind.
void writeFile(const fs::path& p, const std::string& text) {
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << text;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
}

} // namespace

Progress loadProgress() {
    const std::string dir = saveDirectory();
    Progress p;
    if (dir.empty()) return p;
    if (!progressFromJson(readFile(fs::path(dir) / "progress.json"), p)) p = {}; // corrupt or missing: start fresh
    return p;
}

void saveProgress(const Progress& p) {
    const std::string dir = saveDirectory();
    if (!dir.empty()) writeFile(fs::path(dir) / "progress.json", progressToJson(p));
}

Settings loadSettings() {
    Settings s;
    const std::string dir = saveDirectory();
    if (dir.empty()) return s;
    const auto v = json::parse(readFile(fs::path(dir) / "settings.json"));
    if (!v || !v->isObject()) return s;
    s.bloom = (*v)["bloom"].boolean(s.bloom);
    s.quality = std::clamp(static_cast<int>((*v)["quality"].number(s.quality)), 0, 2);
    s.showFps = (*v)["showFps"].boolean(s.showFps);
    s.fullscreen = (*v)["fullscreen"].boolean(s.fullscreen);
    return s;
}

void saveSettings(const Settings& s) {
    const std::string dir = saveDirectory();
    if (dir.empty()) return;
    writeFile(fs::path(dir) / "settings.json",
              json::dump(json::Object{{"bloom", s.bloom}, {"quality", s.quality}, {"showFps", s.showFps}, {"fullscreen", s.fullscreen}}));
}
