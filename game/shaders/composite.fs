#version 330
// Final world composite: scene + bloom, vignette, and a little dithering so
// the dark gradients do not band.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0; // scene
uniform sampler2D bloom;
uniform float bloomStrength;
uniform float vignette;
uniform vec2 resolution;

void main() {
    vec3 c = texture(texture0, fragTexCoord).rgb;
    vec3 b = texture(bloom, fragTexCoord).rgb;
    c += b * bloomStrength;
    // Gentle tone curve so bright cores roll off instead of clipping.
    c = c / (1.0 + max(c - 1.0, 0.0));

    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 d = uv - 0.5;
    d.x *= resolution.x / resolution.y;
    c *= 1.0 - vignette * smoothstep(0.55, 1.25, length(d));

    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    finalColor = vec4(c, 1.0);
}
