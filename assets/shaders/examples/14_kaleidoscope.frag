// N-fold kaleidoscope wrapping a noise pattern for stained-glass look.
// MIT License.

uniform float segments;  // @range 2..16 default=6
uniform float rotate;    // @range -3..3 default=0.4
uniform float zoom;      // @range 0.3..4 default=1.2

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),             hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);
    float a = atan(uv.y, uv.x) + iTime * rotate;

    float seg = 3.14159265 / segments;
    a = mod(a, 2.0 * seg);
    a = abs(a - seg);
    vec2 p = vec2(cos(a), sin(a)) * r * zoom;
    p += iTime * 0.1;

    float v = vnoise(p * 3.0);
    v += 0.5 * vnoise(p * 6.0);

    vec3 col = 0.5 + 0.5 * cos(6.28318 * (v + vec3(0.0, 0.33, 0.67)));
    fragColor = vec4(col, 1.0);
}
