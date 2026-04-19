// Concentric rings radiating from center, quarter-note synced.
// MIT License.

uniform float rings;     // @range 2..32 default=12
uniform float speed;     // @range 0..4 default=1.0
uniform float thickness; // @range 0.01..0.3 default=0.06
uniform float hue;       // @range 0..1 default=0.55

vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);

    float phase = r * rings - iBeat * speed;
    float band = abs(fract(phase) - 0.5) * 2.0;
    float mask = smoothstep(1.0 - thickness, 1.0, 1.0 - band);

    vec3 col = palette(hue + r * 0.5);
    col *= mask;
    col += iKick * 0.25 * palette(hue + 0.5);

    fragColor = vec4(col, 1.0);
}
