#version 330
// Splats one particle into the water field. The falloff is computed here in
// full float precision (not read from an 8-bit texture), which is what keeps
// the summed field free of banding.
//   R = w, G = w * speed, with w = (1 - r^2)^3 on the unit disc.
// Drawn with additive (ONE, ONE) blending into a 16-bit float target.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

void main() {
    vec2 d = fragTexCoord * 2.0 - 1.0;
    float q = max(0.0, 1.0 - dot(d, d));
    float w = q * q * q;
    finalColor = vec4(w, w * fragColor.g, 0.0, w);
}
