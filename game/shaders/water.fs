#version 330
// Metaball water surface.
// texture0 holds an additive "field": every particle splatted a soft radial
// blob. R = summed blob weight, G = summed weight * normalised speed.
// Thresholding R gives one smooth liquid surface instead of many dots.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform float threshold;   // field value at the surface
uniform float rimPx;       // rim width in physical pixels
uniform vec3 slowColor;    // still water
uniform vec3 fastColor;    // fast water
uniform float renderHeight;

void main() {
    vec4 f = texture(texture0, fragTexCoord);
    float field = f.r;

    // Approximate distance to the surface in pixels: (field - iso) / |grad|.
    // This gives an anti-aliased edge and a rim of constant pixel width,
    // independent of how dense the water is.
    float grad = length(vec2(dFdx(field), dFdy(field)));
    float dist = (field - threshold) / max(grad, 1e-5);
    float cover = clamp(dist + 0.5, 0.0, 1.0);
    if (cover <= 0.0) discard;

    // Average speed of the particles contributing to this pixel.
    float speed = clamp(f.g / max(field, 1e-4), 0.0, 1.0);
    vec3 col = mix(slowColor, fastColor, smoothstep(0.05, 0.9, speed));

    // Deeper inside the body (far from the surface) is darker, plus a gentle
    // darkening toward the bottom of the screen.
    float depth = smoothstep(0.0, 60.0 * rimPx / 2.5, dist);
    float yTop = 1.0 - gl_FragCoord.y / renderHeight;
    col *= mix(1.0, 0.62, depth) * mix(1.04, 0.9, yTop);

    // Thin bright rim just inside the surface.
    float rim = exp(-max(dist, 0.0) / rimPx);
    vec3 rimColor = mix(vec3(0.78, 0.72, 1.0), vec3(0.75, 1.0, 1.0), speed);
    col = mix(col, rimColor, rim * 0.75);

    finalColor = vec4(col, cover * mix(0.88, 1.0, rim));
}
