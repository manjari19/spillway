#version 330
// Metaball water surface.
// texture0 is the summed particle field (see blob.fs): R = density-like
// weight, G = weight * speed. One threshold on R gives a single smooth liquid
// surface; G / R is the local average speed, which is smooth too because
// every pixel averages many overlapping kernels.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 texel;        // 1 / target size
uniform float threshold;   // field value at the surface
uniform float rimPx;       // rim width, physical pixels
uniform float dpi;
uniform float time;
uniform vec3 slowColor;    // still water
uniform vec3 fastColor;    // fast water

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    vec4 f = texture(texture0, fragTexCoord);
    float field = f.r;
    if (field < threshold * 0.6) discard;

    // Smooth gradient from central differences 2 px apart (dFdx works on
    // 2x2 pixel blocks and gives stepped rings).
    vec2 o = 2.0 * texel;
    float gx = texture(texture0, fragTexCoord + vec2(o.x, 0)).r - texture(texture0, fragTexCoord - vec2(o.x, 0)).r;
    float gy = texture(texture0, fragTexCoord + vec2(0, o.y)).r - texture(texture0, fragTexCoord - vec2(0, o.y)).r;
    float grad = length(vec2(gx, gy)) / 4.0; // per pixel

    // Signed distance to the surface in pixels (first-order estimate).
    float dist = (field - threshold) / max(grad, 1e-4);
    float cover = smoothstep(-0.8, 0.8, dist);
    if (cover <= 0.0) discard;

    // Interior: smooth blend by speed. Vertically, lighter just under the
    // surface and a deeper violet further down (by distance below the
    // surface, so every pool gets the same gradient whatever its height).
    float speed = smoothstep(0.08, 0.85, clamp(f.g / max(field, 1e-4), 0.0, 1.0));
    // Distance straight up to the free surface (not to walls or floor,
    // which are hidden under terrain): step upward until the field drops
    // below the threshold, then interpolate between the last two samples so
    // the distance is continuous (no banding). Screen-up is +v here.
    const float stepPx = 12.0;
    float up = 96.0 * dpi;
    float prev = field;
    for (int k = 1; k <= 8; ++k) {
        float probe = texture(texture0, fragTexCoord + vec2(0.0, texel.y * stepPx * dpi * float(k))).r;
        if (probe < threshold) {
            float t = clamp((prev - threshold) / max(prev - probe, 1e-4), 0.0, 1.0);
            up = stepPx * dpi * (float(k - 1) + t);
            break;
        }
        prev = probe;
    }
    float below = smoothstep(0.0, 96.0 * dpi, up);
    vec3 col = mix(slowColor, fastColor, speed);
    vec3 deepCol = mix(slowColor * vec3(0.5, 0.4, 0.6), fastColor * vec3(0.4, 0.5, 0.6), speed);
    col = mix(col * 1.1, deepCol, below);

    // Faint slow caustic shimmer in the top part of the water: two drifting
    // noise layers whose ridges brighten where they line up.
    vec2 pc = gl_FragCoord.xy / dpi;
    float c1 = noise(pc * 0.035 + vec2(time * 0.22, time * 0.13));
    float c2 = noise(pc * 0.05 - vec2(time * 0.17, -time * 0.09) + 4.0);
    float caustic = pow(1.0 - abs(c1 - c2), 10.0);
    float nearTop = 1.0 - smoothstep(4.0 * dpi, 46.0 * dpi, up);
    col += mix(vec3(0.55, 0.5, 1.0), fastColor, 0.4) * caustic * nearTop * 0.16;

    // A soft lighter band just under the surface that drifts slowly, so the
    // water looks alive even when still.
    vec2 px = gl_FragCoord.xy / dpi;
    float band = smoothstep(rimPx * 1.5, rimPx * 5.0, dist) * (1.0 - smoothstep(rimPx * 5.0, rimPx * 16.0, dist));
    float wave = 0.5 + 0.5 * sin(px.x * 0.045 + time * 1.3 + sin(px.y * 0.05 - time * 0.7) * 1.6);
    col += mix(slowColor, fastColor, 0.6) * band * wave * 0.22;

    // One thin bright rim exactly at the surface.
    float rim = exp(-max(dist, 0.0) / rimPx);
    col = mix(col, mix(vec3(0.80, 0.74, 1.0), vec3(0.78, 1.0, 1.0), speed), rim * 0.8);

    finalColor = vec4(col, cover * mix(0.9, 1.0, rim));
}
