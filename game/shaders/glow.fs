#version 330
// Soft glow or shadow around a rounded rectangle, from its signed distance
// function. Drawn on a quad that covers the box plus the blur margin; the
// quad's texture coordinates give the position, so it scales correctly with
// any transform (e.g. a card animating in).
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 quadSize;   // logical px
uniform vec2 halfSize;   // half extents of the box, logical px
uniform float radius;    // corner radius
uniform float softness;  // falloff distance outside the box
uniform float inside;    // opacity multiplier inside the box (0 = ring only)

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 p = (fragTexCoord - 0.5) * quadSize;
    float d = sdRoundBox(p, halfSize, radius);
    float a = d > 0.0 ? exp(-(d * d) / (2.0 * softness * softness * 0.25)) : inside;
    finalColor = vec4(fragColor.rgb, fragColor.a * a);
}
