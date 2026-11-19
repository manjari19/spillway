#include "platform.h"

extern "C" {
struct GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
void glfwGetCursorPos(GLFWwindow* window, double* x, double* y);
}

#if defined(__APPLE__)

// AppKit through the Objective-C runtime, so the project stays plain C++.
#include <objc/message.h>
#include <objc/runtime.h>

extern "C" {
// CoreGraphics (linked with the Cocoa framework).
struct CGPoint {
    double x, y;
};
int CGWarpMouseCursorPosition(CGPoint p);
}

namespace {

struct EdgeInsets { // NSEdgeInsets
    double top, left, bottom, right;
};

constexpr unsigned long kStyleMaskFullScreen = 1ul << 14; // NSWindowStyleMaskFullScreen

id window() { return static_cast<id>(GetWindowHandle()); }
SEL sel(const char* name) { return sel_registerName(name); }

template <class R, class... A>
R send(id obj, const char* selector, A... args) {
    return reinterpret_cast<R (*)(id, SEL, A...)>(objc_msgSend)(obj, sel(selector), args...);
}

struct Rect4 { // NSRect
    double x, y, w, h;
};

// Struct returns larger than 16 bytes come back through a hidden pointer on
// x86-64 (objc_msgSend_stret); arm64 returns them normally.
template <class T>
T sendStruct(id obj, const char* selector) {
    T out{};
#if defined(__x86_64__)
    reinterpret_cast<void (*)(T*, id, SEL)>(objc_msgSend_stret)(&out, obj, sel(selector));
#else
    out = send<T>(obj, selector);
#endif
    return out;
}

bool responds(id obj, const char* selector) {
    return obj && send<signed char>(obj, "respondsToSelector:", sel(selector));
}

} // namespace

namespace platform {

bool isFullscreen() {
    id w = window();
    return w && (send<unsigned long>(w, "styleMask") & kStyleMaskFullScreen) != 0;
}

void setFullscreen(bool on) {
    id w = window();
    if (!w || isFullscreen() == on) return;
    // Animated, asynchronous: the window resizes over the next ~0.5 s and
    // the game re-lays out as the size comes in.
    send<void>(w, "toggleFullScreen:", static_cast<id>(nullptr));
}

Insets safeArea() {
    Insets out;
    id w = window();
    if (!w || !isFullscreen()) return out;
    id screen = send<id>(w, "screen");
    if (!responds(screen, "safeAreaInsets")) return out; // before macOS 12: no notch
    const EdgeInsets e = sendStruct<EdgeInsets>(screen, "safeAreaInsets");
    // The insets describe the screen. Depending on the macOS version and
    // settings, a fullscreen window may already sit below the notch band;
    // only the part of the band that overlaps the window counts. (AppKit
    // frames have their origin at the bottom-left.)
    const Rect4 sf = sendStruct<Rect4>(screen, "frame"), wf = sendStruct<Rect4>(w, "frame");
    const double gapAbove = (sf.y + sf.h) - (wf.y + wf.h);
    // AppKit points are the game's logical pixels.
    out.top = static_cast<float>(e.top > gapAbove ? e.top - gapAbove : 0.0);
    out.left = static_cast<float>(e.left);
    out.bottom = static_cast<float>(e.bottom);
    out.right = static_cast<float>(e.right);
    return out;
}

bool probeCursor(Vector2 p, Vector2& reported) {
    GLFWwindow* gw = glfwGetCurrentContext();
    if (!gw) return false;
    // Global display coordinates: origin at the top-left of the main display.
    const Vector2 origin = GetWindowPosition();
    CGWarpMouseCursorPosition({origin.x + p.x, origin.y + p.y});
    double x = 0, y = 0;
    glfwGetCursorPos(gw, &x, &y);
    reported = {static_cast<float>(x), static_cast<float>(y)};
    return true;
}

} // namespace platform

#else

namespace platform {

bool isFullscreen() { return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); }

void setFullscreen(bool on) {
    if (isFullscreen() != on) ToggleBorderlessWindowed();
}

Insets safeArea() { return {}; }

bool probeCursor(Vector2 p, Vector2& reported) {
    (void)p;
    (void)reported;
    return false;
}

} // namespace platform

#endif
