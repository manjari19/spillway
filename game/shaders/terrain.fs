#version 330
// Terrain shading: dark, glowy, soft.
//
// Inputs
//   texture0   marching-squares meshes, R = dirt, G = rock, B = wall coverage
//   field      smoothed solid occupancy, one texel per cell. 0.5 is the drawn
//              outline; its gradient gives the outline normal and, divided
//              into (f - 0.5), the distance to the outline in pixels.
//   sdf        exact signed distance in metres (deep interior shading)
//   waterField the water's metaball field this frame, for water-lit edges
//   dugTime    per cell: time it was last dug next to (fresh-cut glow)

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D sdf;
uniform sampler2D field;
uniform sampler2D waterField;
uniform sampler2D dugTime;
uniform vec2 sdfSize;        // nodes (w + 1, h + 1)
uniform vec2 fieldSize;      // cells (w, h)
uniform float cellSize;      // metres
uniform vec2 levelSize;      // metres
uniform vec2 texel;          // 1 / render size
uniform float renderHeight;
uniform float dpi;
uniform vec2 viewOffset;     // logical px
uniform float viewScale;     // logical px per metre
uniform float time;

uniform vec3 strataA;        // plum
uniform vec3 strataB;        // indigo
uniform vec3 strataC;        // violet accent band
uniform vec3 rimColor;       // lavender rim light
uniform vec3 waterTint;      // cyan-violet, near water
uniform vec3 rockBody;       // obsidian
uniform vec3 rockEdge;       // lavender edge glow
uniform vec3 rockEdgeAlt;    // cyan edge glow
uniform vec3 wallColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int k = 0; k < 4; ++k) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}
// Voronoi: (nearest distance, second nearest distance, id of nearest cell).
vec3 voronoi(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float d1 = 8.0, d2 = 8.0, id = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(x, y);
            vec2 o = vec2(hash(i + g), hash(i + g + 17.3));
            float d = length(g + o - f);
            if (d < d1) { d2 = d1; d1 = d; id = hash(i + g + 5.1); }
            else if (d < d2) { d2 = d; }
        }
    return vec3(d1, d2, id);
}

float fieldAt(vec2 world) { return texture(field, world / (cellSize * fieldSize)).r; }

void main() {
    // Material coverage with a small 5-tap soften (cheap anti-aliasing).
    vec4 m = texture(texture0, fragTexCoord) * 0.4;
    m += texture(texture0, fragTexCoord + vec2( 0.6,  0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2(-0.6,  0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2( 0.6, -0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2(-0.6, -0.6) * texel) * 0.15;
    float cover = m.r + m.g + m.b;
    if (cover < 0.004) discard;
    vec3 w = m.rgb / cover;

    // World position (gl_FragCoord.y counts from the bottom).
    vec2 screen = vec2(gl_FragCoord.x, renderHeight - gl_FragCoord.y) / dpi;
    vec2 world = (screen - viewOffset) / viewScale;

    // Distance (logical px) inside the outline and the outline normal.
    float f = fieldAt(world);
    float e = cellSize;
    vec2 g = vec2(fieldAt(world + vec2(e, 0)) - fieldAt(world - vec2(e, 0)),
                  fieldAt(world + vec2(0, e)) - fieldAt(world - vec2(0, e))) / (2.0 * e);
    float gradPx = length(g) / viewScale;
    float distPx = gradPx > 1e-5 ? (f - 0.5) / gradPx : 1e4;
    vec2 n = -g / max(length(g), 1e-5);
    float facingUp = smoothstep(0.1, 0.8, -n.y);
    float rim = 1.0 - smoothstep(1.2, 3.8, distPx);    // ~2-4 px rim
    float halo = 1.0 - smoothstep(0.0, 16.0, distPx);  // soft inner glow
    float depth = clamp(-texture(sdf, (world / cellSize + 0.5) / sdfSize).r, 0.0, 0.3);

    // Water nearby (its field spreads ~0.3 m past the water surface).
    float nearWater = smoothstep(0.1, 1.3, texture(waterField, fragTexCoord).r);

    // Freshly dug edges glow, fading over ~1 s.
    float dug = time - texture(dugTime, world / (cellSize * fieldSize)).r;
    float fresh = clamp(1.0 - dug, 0.0, 1.0);
    fresh *= fresh;

    // ---------------------------------------------------------------- dirt
    // Sediment strata: horizontal bands in world space, gently warped by
    // low-frequency noise, so any cut reveals the same continuous layers.
    float warp = fbm(world * vec2(0.18, 0.45)) * 1.6 + sin(world.x * 0.33) * 0.35;
    float v = world.y * 1.25 + warp;
    float bandA = 0.5 + 0.5 * sin(v * 6.2831);
    float bandB = 0.5 + 0.5 * sin(v * 6.2831 * 2.3 + 1.7);
    vec3 dirt = mix(strataA, strataB, smoothstep(0.15, 0.85, bandA));
    dirt = mix(dirt, strataC, 0.3 * smoothstep(0.45, 0.95, bandB));
    // Deeper = darker and greyer: lower in the level, and further below the surface.
    float lower = clamp(world.y / levelSize.y, 0.0, 1.0);
    float deep = 0.6 * lower + 0.4 * smoothstep(0.04, 0.3, depth);
    float lum = dot(dirt, vec3(0.3, 0.5, 0.2));
    dirt = mix(dirt, vec3(lum) * vec3(0.92, 0.9, 1.1), deep * 0.4) * (1.0 - deep * 0.28);
    dirt *= 0.975 + 0.05 * noise(world * 80.0);  // very fine grain
    // Rim light on exposed edges, lit cyan-violet where water is close.
    vec3 lit = mix(rimColor, waterTint, nearWater);
    dirt += lit * (rim * (0.42 + 0.2 * facingUp) + halo * 0.07) * (1.0 + 1.3 * nearWater);
    dirt += vec3(0.75, 0.65, 1.0) * fresh * (rim * 1.6 + halo * 0.9);

    // ---------------------------------------------------------------- rock
    // Obsidian: near-black blue body with a few large, soft, low-contrast
    // facets and a thin glowing edge line.
    vec3 vo = voronoi(world * 0.75);
    float facet = (vo.z - 0.5) * 0.22;
    float facetLine = 1.0 - smoothstep(0.0, 0.06, vo.y - vo.x);
    float sheen = fbm(world * 0.6 + vo.z * 3.0) * 0.12;
    vec3 rock = rockBody * (1.0 + facet + sheen) + rockEdge * facetLine * 0.03;
    // Lit from above: the top of each rock is a little lighter, so a
    // boulder reads as a solid object, not a hole.
    rock += rockEdge * 0.06 * facingUp * (1.0 - smoothstep(0.0, 0.25, depth));
    rock += rockEdge * 0.035 * (1.0 - smoothstep(0.0, 0.3, depth));
    // The level border frames the level: darker and quieter.
    float borderDist = min(min(world.x, world.y), min(levelSize.x - world.x, levelSize.y - world.y));
    float quiet = 1.0 - smoothstep(0.34, 0.5, borderDist);
    rock *= 1.0 - 0.3 * quiet;
    float pulse = 0.85 + 0.15 * sin(time * 1.1 + world.x * 0.7 + world.y * 0.45);
    vec3 edgeCol = mix(rockEdge, rockEdgeAlt, 0.5 + 0.5 * sin(world.x * 0.5 - world.y * 0.3 + time * 0.25));
    float rockGlow = (rim * 0.95 + halo * 0.16) * pulse * (1.0 - 0.7 * quiet);
    rock += edgeCol * rockGlow;

    // --------------------------------------------------------------- walls
    vec3 wall = wallColor * (0.92 + 0.12 * smoothstep(0.5, 0.95, f));

    vec3 col = w.r * dirt + w.g * rock + w.b * wall;

    // Rock outline where it meets dirt (no empty space there, so the rim
    // above does not fire): the rock coverage changes across ~1.5 px.
    vec2 o = 1.4 * dpi * texel;
    float r0 = texture(texture0, fragTexCoord + vec2(o.x, 0)).g, r1 = texture(texture0, fragTexCoord - vec2(o.x, 0)).g;
    float r2 = texture(texture0, fragTexCoord + vec2(0, o.y)).g, r3 = texture(texture0, fragTexCoord - vec2(0, o.y)).g;
    float rockLine = max(max(r0, r1), max(r2, r3)) - min(min(r0, r1), min(r2, r3));
    col += edgeCol * rockLine * 0.7 * pulse * (1.0 - 0.7 * quiet) * (1.0 - rim);

    finalColor = vec4(col, clamp(cover, 0.0, 1.0));
}
