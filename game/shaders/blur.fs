#version 330
// Separable Gaussian blur (one direction per pass). 9 taps, done as 5
// bilinear fetches by sampling between texels.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 direction; // (1/width, 0) or (0, 1/height), times spread

void main() {
    const float w0 = 0.2270270270, w1 = 0.3162162162, w2 = 0.0702702703;
    const float o1 = 1.3846153846, o2 = 3.2307692308;
    vec4 c = texture(texture0, fragTexCoord) * w0;
    c += texture(texture0, fragTexCoord + direction * o1) * w1;
    c += texture(texture0, fragTexCoord - direction * o1) * w1;
    c += texture(texture0, fragTexCoord + direction * o2) * w2;
    c += texture(texture0, fragTexCoord - direction * o2) * w2;
    finalColor = c;
}
