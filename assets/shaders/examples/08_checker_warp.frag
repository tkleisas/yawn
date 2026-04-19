// Warped checkerboard — a cheap chequer grid pulled around by time + kick.
// MIT License.

uniform float cells;    // @range 2..40 default=12
uniform float warp;     // @range 0..1 default=0.35
uniform float speed;    // @range 0..3 default=0.6

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = uv - 0.5;

    // Apply a smooth swirl + kick shake.
    float ang = length(p) * (3.0 + iKick * 4.0) + iTime * speed;
    float s = sin(ang) * warp;
    float c = cos(ang) * warp;
    p += vec2(c * p.y, -s * p.x);

    vec2 g = p * cells;
    float check = mod(floor(g.x) + floor(g.y), 2.0);
    vec3 a = vec3(0.08, 0.10, 0.15);
    vec3 b = vec3(0.95, 0.85, 0.70);
    vec3 col = mix(a, b, check);

    fragColor = vec4(col, 1.0);
}
