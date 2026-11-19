#pragma once

// All game input goes through here instead of calling raylib directly.
//
// Normally this just forwards to raylib. With SPILLWAY_INPUT set, a script
// drives it instead (real input is ignored), so the real UI can be played
// through automatically: menus, clicks, transitions, everything. Used to
// capture screenshots from the live interactive path.
//
// Script: ';'-separated "time:action" entries, time in seconds since start:
//   0.5:key:ENTER        press a key for one frame
//   1.0:move:640,360     move the mouse (logical px)
//   1.2:click:640,360    move there and click the left button
//   2.0:drag:x0,y0,x1,y1,0.6   hold the left button while moving over 0.6 s
//   3.0:shot:/path.png   save a screenshot of the frame
//   4.0:quit

#include <raylib.h>

#include <string>

namespace input {

void loadScript(const std::string& spec);
bool scripted();
// Advance the script to time `now`; call once at the start of every frame.
void beginFrame(double now);

bool pressed(int key);
bool down(int key);
bool mousePressed(int button);
bool mouseDown(int button);
Vector2 mouse();
float wheel();
bool cursorOnScreen();

// Script requests for the app.
bool takeShot(std::string& path);
bool quitRequested();

} // namespace input
