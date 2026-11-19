#include "input.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace input {
namespace {

struct Event {
    double t;
    std::string action;
    std::string args;
};

struct State {
    bool active = false;
    std::vector<Event> events;
    size_t next = 0;
    std::set<int> pressedKeys; // this frame only
    bool click = false;        // left button pressed this frame
    bool held = false;         // left button down
    Vector2 pos{0, 0};
    // Drag in progress.
    bool dragging = false;
    double dragStart = 0, dragDur = 0;
    Vector2 dragFrom{}, dragTo{};
    std::vector<std::string> shots;
    std::vector<Vector2> probes;
    int osToggles = 0;
    bool quit = false;
    std::set<int> consumed; // keys already handled this frame (real or scripted)
} g;

int keyCode(const std::string& name) {
    static const std::map<std::string, int> keys{
        {"ENTER", KEY_ENTER}, {"ESCAPE", KEY_ESCAPE}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT},
        {"RIGHT", KEY_RIGHT}, {"SPACE", KEY_SPACE}, {"R", KEY_R}, {"P", KEY_P}, {"G", KEY_G}, {"B", KEY_B},
        {"ONE", KEY_ONE}, {"TWO", KEY_TWO}, {"THREE", KEY_THREE}, {"GRAVE", KEY_GRAVE},
        {"LBRACKET", KEY_LEFT_BRACKET}, {"RBRACKET", KEY_RIGHT_BRACKET}, {"F", KEY_F},
        {"ALT", KEY_LEFT_ALT}, {"CMD", KEY_LEFT_SUPER}, {"CTRL", KEY_LEFT_CONTROL}};
    auto it = keys.find(name);
    return it == keys.end() ? KEY_NULL : it->second;
}

} // namespace

void loadScript(const std::string& spec) {
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const size_t a = item.find(':');
        if (a == std::string::npos) continue;
        const size_t b = item.find(':', a + 1);
        Event e{std::atof(item.substr(0, a).c_str()), item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1),
                b == std::string::npos ? "" : item.substr(b + 1)};
        g.events.push_back(e);
    }
    g.active = !g.events.empty();
}

bool scripted() { return g.active; }

void beginFrame(double now) {
    g.consumed.clear();
    if (!g.active) return;
    g.pressedKeys.clear();
    g.click = false;
    if (g.dragging) {
        const float t = g.dragDur > 0 ? static_cast<float>((now - g.dragStart) / g.dragDur) : 1.0f;
        const float k = t < 0 ? 0 : t > 1 ? 1 : t;
        g.pos = {g.dragFrom.x + (g.dragTo.x - g.dragFrom.x) * k, g.dragFrom.y + (g.dragTo.y - g.dragFrom.y) * k};
        if (t >= 1.0f) { g.dragging = false; g.held = false; }
    }
    while (g.next < g.events.size() && g.events[g.next].t <= now) {
        const Event& e = g.events[g.next++];
        float v[5] = {0, 0, 0, 0, 0};
        std::sscanf(e.args.c_str(), "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
        if (e.action == "key") {
            // "ALT+ENTER": every key of a chord is down (and pressed) this frame.
            std::stringstream chord(e.args);
            std::string k;
            while (std::getline(chord, k, '+')) g.pressedKeys.insert(keyCode(k));
        }
        else if (e.action == "probe") g.probes.push_back({v[0], v[1]});
        else if (e.action == "ostoggle") ++g.osToggles;
        else if (e.action == "move") g.pos = {v[0], v[1]};
        else if (e.action == "click") { g.pos = {v[0], v[1]}; g.click = true; }
        else if (e.action == "drag") {
            g.dragFrom = {v[0], v[1]};
            g.dragTo = {v[2], v[3]};
            g.dragStart = now;
            g.dragDur = v[4];
            g.dragging = true;
            g.held = true;
            g.click = true;
            g.pos = g.dragFrom;
        } else if (e.action == "shot") g.shots.push_back(e.args);
        else if (e.action == "quit") g.quit = true;
    }
}

bool pressed(int key) {
    if (g.consumed.count(key)) return false;
    return g.active ? g.pressedKeys.count(key) > 0 : IsKeyPressed(key);
}
void consume(int key) { g.consumed.insert(key); }
bool down(int key) { return g.active ? g.pressedKeys.count(key) > 0 : IsKeyDown(key); }
bool mousePressed(int button) { return g.active ? (button == MOUSE_BUTTON_LEFT && g.click) : IsMouseButtonPressed(button); }
bool mouseDown(int button) { return g.active ? (button == MOUSE_BUTTON_LEFT && (g.held || g.click)) : IsMouseButtonDown(button); }
Vector2 mouse() { return g.active ? g.pos : GetMousePosition(); }
float wheel() { return g.active ? 0.0f : GetMouseWheelMove(); }
bool cursorOnScreen() { return g.active ? true : IsCursorOnScreen(); }

bool takeShot(std::string& path) {
    if (g.shots.empty()) return false;
    path = g.shots.front();
    g.shots.erase(g.shots.begin());
    return true;
}
bool takeProbe(Vector2& at) {
    if (g.probes.empty()) return false;
    at = g.probes.front();
    g.probes.erase(g.probes.begin());
    return true;
}
bool takeOsToggle() {
    if (g.osToggles == 0) return false;
    --g.osToggles;
    return true;
}
bool quitRequested() { return g.quit; }

} // namespace input
