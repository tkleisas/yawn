// Flow-noise field — value-noise gradient drives a domain warp.
// MIT License.

uniform float speed;     // @range 0..2 default=0.5
uniform float scale;     // @range 0.5..8 default=3.0
uniform float warp;      // @range 0..2 default=1.0
uniform float tint;      // @range 0..1 default=0.6

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1, 0));
    float c = hash(i + vec2(0, 1));
    float d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p  = uv * scale;
    float t = iTime * speed;

    vec2 off = vec2(vnoise(p + t), vnoise(p - t)) - 0.5;
    float v  = vnoise(p + off * warp + t * 0.2);

    vec3 cool = vec3(0.1, 0.4, 0.9);
    vec3 warm = vec3(0.9, 0.5, 0.2);
    vec3 col = mix(cool, warm, v);
    col = mix(vec3(v), col, tint);
    fragColor = vec4(col, 1.0);
}
