#pragma once

// Replays a recorded level solution headlessly.
//
// Solution file format (tests/solutions/levelNN.txt), one command per line:
//   expect <won|lost> <stars>
//   at <step> <dig|wall|erase> <radius> <x0> <y0> <x1> <y1>
// Steps are fixed physics steps (1/60 s). Edits are queued before that step
// runs, exactly like the game does with mouse input, so a replay is
// bit-identical to playing it by hand with the same inputs.

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "spillway/world.h"

namespace replay {

struct TimedEdit {
    int step = 0;
    spillway::Edit edit;
};

struct Solution {
    bool expectWin = true;
    int expectStars = 1;
    std::vector<TimedEdit> edits;
    std::string error;
};

inline Solution loadSolution(const std::string& path) {
    Solution s;
    std::ifstream f(path);
    if (!f) { s.error = "cannot open " + path; return s; }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        std::string cmd;
        if (!(in >> cmd)) continue;
        if (cmd == "expect") {
            std::string result;
            in >> result >> s.expectStars;
            s.expectWin = result == "won";
        } else if (cmd == "at") {
            TimedEdit t;
            std::string tool;
            in >> t.step >> tool >> t.edit.radius >> t.edit.from.x >> t.edit.from.y >> t.edit.to.x >> t.edit.to.y;
            t.edit.tool = tool == "wall" ? spillway::Tool::Wall : tool == "erase" ? spillway::Tool::Erase : spillway::Tool::Dig;
            if (!in) { s.error = path + ":" + std::to_string(n) + ": bad edit line"; return s; }
            s.edits.push_back(t);
        } else {
            s.error = path + ":" + std::to_string(n) + ": unknown command " + cmd;
            return s;
        }
    }
    return s;
}

// Runs until the level finishes (or maxSteps). Returns the steps taken.
inline int run(spillway::World& w, const Solution& s, int maxSteps = 60 * 240) {
    size_t next = 0;
    int step = 0;
    for (; step < maxSteps && !w.finished(); ++step) {
        while (next < s.edits.size() && s.edits[next].step <= step) w.queueEdit(s.edits[next++].edit);
        w.step();
        w.events().clear();
    }
    return step;
}

} // namespace replay
