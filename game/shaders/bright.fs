#version 330
// Bloom bright pass: keep only what is brighter than a threshold, with a
// soft knee so the cut-off does not create hard edges.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform float threshold;
uniform float knee;

void main() {
    vec3 c = texture(texture0, fragTexCoord).rgb;
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contrib = max(soft, br - threshold) / max(br, 1e-4);
    finalColor = vec4(c * contrib, 1.0);
}
