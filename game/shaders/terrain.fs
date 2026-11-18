#version 330
// Terrain shading.
// texture0: marching-squares meshes rendered with R = dirt, G = rock,
//           B = wall coverage.
// sdf:      the engine's signed distance field (metres, negative inside),
//           used for edge highlights and inner shading.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D sdf;
uniform vec2 sdfSize;        // nodes (w + 1, h + 1)
uniform float cellSize;      // metres
uniform vec2 texel;          // 1 / render size
uniform float renderHeight;
uniform float dpi;
uniform vec2 viewOffset;     // logical px
uniform float viewScale;     // logical px per metre

uniform vec3 dirtColor;
uniform vec3 dirtTopColor;
uniform vec3 rockColor;
uniform vec3 rockEdgeColor;
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

float phiAt(vec2 world) { return texture(sdf, (world / cellSize + 0.5) / sdfSize).r; }

void main() {
    // Soften the rasterised mesh edge with a 5-tap average (cheap AA).
    vec4 m = texture(texture0, fragTexCoord) * 0.4;
    m += texture(texture0, fragTexCoord + vec2( 0.6,  0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2(-0.6,  0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2( 0.6, -0.6) * texel) * 0.15;
    m += texture(texture0, fragTexCoord + vec2(-0.6, -0.6) * texel) * 0.15;
    float cover = m.r + m.g + m.b;
    if (cover < 0.004) discard;
    vec3 w = m.rgb / cover;

    // World position of this pixel (gl_FragCoord.y is measured from the bottom).
    vec2 screen = vec2(gl_FragCoord.x, renderHeight - gl_FragCoord.y) / dpi;
    vec2 world = (screen - viewOffset) / viewScale;

    // Distance below the surface and the outward surface normal.
    float phi = phiAt(world);
    float e = cellSize;
    vec2 g = vec2(phiAt(world + vec2(e, 0)) - phiAt(world - vec2(e, 0)),
                  phiAt(world + vec2(0, e)) - phiAt(world - vec2(0, e)));
    vec2 n = g / max(length(g), 1e-5);
    float depth = max(-phi, 0.0);                          // metres inside
    float edge = 1.0 - smoothstep(0.0, 0.07, depth);       // near the surface
    float facingUp = smoothstep(0.15, 0.85, -n.y);         // +y is down

    // Dirt: warm plum with soft blotches and grain, a lighter top lip, and
    // darker as you go deeper.
    float blot = fbm(world * 2.2);
    float grain = noise(world * 38.0);
    vec3 dirt = dirtColor * (0.86 + 0.28 * blot) * (0.94 + 0.12 * grain);
    dirt = mix(dirt, dirtTopColor, edge * (0.35 + 0.65 * facingUp));
    dirt *= mix(1.0, 0.7, smoothstep(0.06, 0.3, depth));

    // Rock: cool slate with larger, harder-edged facets and a faint cold rim
    // on every side, so it reads as a different, undiggable material.
    float facet = floor(fbm(world * 1.4) * 5.0) / 5.0;
    vec3 rock = rockColor * (0.85 + 0.3 * facet) * (0.97 + 0.06 * noise(world * 22.0));
    rock = mix(rock, rockEdgeColor, edge * (0.45 + 0.35 * facingUp));
    rock *= mix(1.0, 0.8, smoothstep(0.06, 0.3, depth));

    // Walls: soft glowing white with a slightly brighter core (bloom picks
    // this up).
    vec3 wall = wallColor * (1.0 + 0.15 * smoothstep(0.02, 0.1, depth));

    vec3 col = w.r * dirt + w.g * rock + w.b * wall;
    finalColor = vec4(col, clamp(cover, 0.0, 1.0));
}
