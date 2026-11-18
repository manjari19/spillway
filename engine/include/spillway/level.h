#pragma once

// Level description and a small text format to load it from.
//
// A level file is a list of commands, one per line; '#' starts a comment.
// Terrain commands are applied in order, painter-style, onto a grid that
// starts EMPTY, so later shapes overwrite earlier ones:
//
//   name   First Channel             display name (rest of line)
//   hint   Dig a path to the drain.  one-line tip shown in the HUD
//   size   16 9                      level size in metres (required)
//   cell   0.075                     terrain cell size in metres (optional)
//   win    70                        % of water that must reach a drain
//   ink    6                         wall ink, in metres of default-width wall
//
//   dirt   rect   x0 y0 x1 y1        material + shape. Materials: empty,
//   rock   circle cx cy r            dirt, rock. (Walls are player-drawn
//   empty  poly   x y x y x y ...    only.) Shapes: rect, circle, poly.
//
//   water  rect x0 y0 x1 y1          fill with fluid at rest density
//   drain  rect x0 y0 x1 y1          particles here are saved
//   pit    rect x0 y0 x1 y1          particles here are lost
//
// Coordinates are metres, origin top-left, +y down. Shapes describe areas
// instead of listing cells, so a level stays readable, slopes are just
// polygons, and the same file works at any cell size.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "spillway/terrain.h"
#include "spillway/vec2.h"

namespace spillway {

struct Rect {
    Vec2 min{};
    Vec2 max{};
    bool contains(Vec2 p) const { return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y; }
};

struct Shape {
    enum class Kind { Rect, Circle, Polygon };
    Kind kind = Kind::Rect;
    std::vector<Vec2> points; // rect: {min, max}; circle: {centre}; polygon: vertices
    float radius = 0.0f;
    bool contains(Vec2 p) const;
};

struct TerrainOp {
    Cell material = Cell::Dirt;
    Shape shape;
};

struct LevelDesc {
    std::string name = "Untitled";
    std::string hint;
    Vec2 size{};
    float cellSize = 0.075f;
    float winPercent = 70.0f;
    float inkMetres = 0.0f;
    std::vector<TerrainOp> terrain;
    std::vector<Rect> water;
    std::vector<Rect> drains;
    std::vector<Rect> pits;
};

struct LevelLoadResult {
    std::optional<LevelDesc> level; // set on success
    std::string error;              // e.g. "line 7: unknown command 'drian'"
    explicit operator bool() const { return level.has_value(); }
};

LevelLoadResult parseLevel(std::string_view text);
LevelLoadResult loadLevelFile(const std::string& path);

// Rasterises the terrain commands: a cell takes the material of the last
// shape containing its centre. Also builds the SDF.
Terrain buildTerrain(const LevelDesc& level);

} // namespace spillway
