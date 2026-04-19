// Animated Voronoi cells with mid-band energy modulating cell jitter.
// MIT License.

uniform float cells;     // @range 2..20 default=6
uniform float jitter;    // @range 0..1 default=0.6
uniform float speed;     // @range 0..2 default=0.4

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = uv * cells;

    vec2 i = floor(p);
    vec2 f = fract(p);

    float j = mix(0.0, jitter, 0.6 + 0.4 * iAudioMid);

    float minD = 8.0;
    vec2 minOff = vec2(0.0);
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec2 n = vec2(x, y);
        vec2 r = hash2(i + n);
        r = 0.5 + (r - 0.5) * j;
        r = r + 0.5 * sin(iTime * speed + 6.28318 * r);
        vec2 off = n + r - f;
        float d = dot(off, off);
        if (d < minD) { minD = d; minOff = n + r; }
    }

    float cell = hash2(i + floor(minOff)).x;
    vec3 col = 0.5 + 0.5 * cos(6.28318 * (cell + vec3(0.1, 0.4, 0.7)));
    col *= smoothstep(0.6, 0.0, sqrt(minD));
    fragColor = vec4(col, 1.0);
}
