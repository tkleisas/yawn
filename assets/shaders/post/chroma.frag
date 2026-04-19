// Chromatic aberration — radial RGB split from screen center, audio-reactive.
// MIT License.

uniform float shift;    // @range 0..0.04 default=0.004
uniform float reactive; // @range 0..3 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 center = uv - 0.5;
    float s = shift * (1.0 + reactive * iAudioLow * 2.0);

    float r = texture(iPrev, uv + center * s).r;
    float g = texture(iPrev, uv).g;
    float b = texture(iPrev, uv - center * s).b;
    fragColor = vec4(r, g, b, 1.0);
}
