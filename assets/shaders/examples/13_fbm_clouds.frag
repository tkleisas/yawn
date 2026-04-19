// Fractal Brownian motion "clouds" — five octaves of value noise.
// MIT License.

uniform float scale;    // @range 0.5..8 default=2.5
uniform float speed;    // @range 0..2 default=0.3
uniform float density;  // @range 0..3 default=1.2
uniform float tint;     // @range 0..1 default=0.4

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

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

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * vnoise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = uv * scale + iTime * speed * vec2(0.3, 0.1);
    float v = fbm(p) * density;

    vec3 warm = vec3(1.0, 0.7, 0.4);
    vec3 cool = vec3(0.2, 0.4, 0.8);
    vec3 col = mix(cool, warm, smoothstep(0.3, 0.9, v));
    col = mix(vec3(v), col, tint);
    fragColor = vec4(col, 1.0);
}
