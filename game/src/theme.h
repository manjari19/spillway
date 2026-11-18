#pragma once

// The one place colours and type sizes are defined.

#include <raylib.h>

namespace theme {

// Background
inline constexpr Color bgTop{0x0b, 0x0a, 0x1a, 255};
inline constexpr Color bgBottom{0x05, 0x04, 0x0d, 255};
inline constexpr Color levelTop{0x13, 0x11, 0x2c, 255}; // open space inside a level
inline constexpr Color levelBottom{0x0a, 0x09, 0x1a, 255};

// Water: violet when still, cyan when fast
inline constexpr Color waterSlow{0x6d, 0x3c, 0xff, 255};
inline constexpr Color waterFast{0x3e, 0xe8, 0xff, 255};

// Terrain
inline constexpr Color dirt{0x3a, 0x24, 0x33, 255};
inline constexpr Color dirtTop{0x5a, 0x3a, 0x4c, 255};
inline constexpr Color rock{0x2a, 0x2f, 0x45, 255};
inline constexpr Color rockEdge{0x46, 0x50, 0x72, 255};
inline constexpr Color wall{0xe8, 0xe6, 0xff, 255};

// Accents (used sparingly)
inline constexpr Color violet = waterSlow;
inline constexpr Color cyan = waterFast;
inline constexpr Color warm{0xff, 0xb8, 0x86, 255};   // dig brush
inline constexpr Color danger{0xd9, 0x4a, 0x9a, 255}; // pits, failure
inline constexpr Color star{0xbf, 0xf6, 0xff, 255};

// Text
inline constexpr Color text{0xec, 0xeb, 0xff, 255};
inline constexpr Color textDim{0x9a, 0x96, 0xc4, 255};
inline constexpr Color textMuted{0x9a, 0x96, 0xc4, 110};

// Glass UI
inline constexpr Color glassFill{20, 17, 42, 175};
inline constexpr Color glassBorder{255, 255, 255, 28};
inline constexpr Color glassSheen{255, 255, 255, 10};

// Type scale in logical pixels at 1280x720 (multiplied by ui::scale()).
namespace type {
inline constexpr float display = 112.0f; // SPILLWAY
inline constexpr float title = 36.0f;    // card titles
inline constexpr float heading = 28.0f;  // menu items
inline constexpr float body = 18.0f;
inline constexpr float small = 15.0f;
inline constexpr float caption = 12.5f;  // uppercase labels
} // namespace type

} // namespace theme
