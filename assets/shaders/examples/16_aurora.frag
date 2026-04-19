// Soft northern-lights bands — vertical gradient + sinuous mid-band shift.
// MIT License.

uniform float bands;    // @range 1..6 default=3
uniform float speed;    // @range 0..1.5 default=0.3
uniform float bright;   // @range 0..2 default=1.0

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),             hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    float total = 0.0;
    for (int i = 0; i < 6; i++) {
        if (float(i) >= bands) break;
        float o = float(i) * 0.15;
        float y = 0.5 + 0.3 * sin(uv.x * 3.0 + iTime * speed + o * 6.28) +
                        0.05 * vnoise(uv * 6.0 + iTime * speed);
        float d = abs(uv.y - y);
        total += exp(-d * d * 80.0);
    }

    vec3 green = vec3(0.1, 1.0, 0.55);
    vec3 violet = vec3(0.6, 0.2, 0.9);
    vec3 col = mix(green, violet, uv.y) * total * bright;
    col += vec3(0.02, 0.04, 0.08);
    fragColor = vec4(col, 1.0);
}
