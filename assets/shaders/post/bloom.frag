// Bloom — thresholded blur added on top of the source. Post-FX; samples
// the previous stage via iPrev and writes a new image.
// MIT License.

uniform float threshold; // @range 0..1 default=0.55
uniform float intensity; // @range 0..3 default=1.0
uniform float radius;    // @range 1..8 default=3.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy * radius;

    vec3 base = texture(iPrev, uv).rgb;

    // 5x5 box blur on whatever is above the threshold.
    vec3 sum = vec3(0.0);
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec3 s = texture(iPrev, uv + vec2(x, y) * px).rgb;
            vec3 bright = max(s - vec3(threshold), 0.0);
            sum += bright;
        }
    }
    vec3 glow = (sum / 25.0) * intensity;
    fragColor = vec4(base + glow, 1.0);
}
