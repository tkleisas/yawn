// Morphing 4D wireframe: point → square → cube → hypercube (tesseract).
// `morph` unfolds it one dimension at a time; `hue` sets the wire colour.
// Drawn as projected line segments (4D → 3D → 2D, both perspective).
// MIT License.

uniform float morph;     // @range 0..1   default=1.0  point→square→cube→hypercube
uniform float hue;       // @range 0..1   default=0.58 wire colour
uniform float speed;     // @range 0..3   default=0.5  tumble speed
uniform float thickness; // @range 0.5..4 default=1.5  line thickness
uniform float scale;     // @range 0.2..3 default=1.0  wireframe size

vec3 hue2rgb(float h) {
    vec3 k = abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0;
    return clamp(k, 0.0, 1.0);
}
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

// Hypercube vertex `idx` (0..15) → its 4D corner, scaled per-dimension by the
// morph extents so dimensions unfold one stage at a time.
vec4 hcVertex(int idx, vec4 ext) {
    return vec4(
        float((idx       & 1) * 2 - 1),
        float((idx >> 1) & 1) * 2.0 - 1.0,
        float((idx >> 2) & 1) * 2.0 - 1.0,
        float((idx >> 3) & 1) * 2.0 - 1.0
    ) * ext * 0.65;
}

// 4D corner → 2D screen point: 4D tumble, perspective to 3D, spin, perspective to 2D.
vec2 hcProject(vec4 p, float t) {
    p.xw = rot(t * 0.31) * p.xw;          // 4D rotation (two planes)
    p.yz = rot(t * 0.27) * p.yz;
    float kw = 1.0 / (2.6 - p.w);          // 4D → 3D perspective (along w)
    vec3 q = p.xyz * kw * 2.6;
    q.xz = rot(t * 0.50) * q.xz;           // 3D spin
    q.yz = rot(t * 0.37) * q.yz;
    float kz = 1.0 / (3.4 - q.z);          // 3D → 2D perspective (along z)
    return q.xy * kz * 3.4;
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // Unfold one dimension (pair) at a time: x,y over [0,1/3] (point→square),
    // z over [1/3,2/3] (square→cube), w over [2/3,1] (cube→hypercube).
    float m   = clamp(morph, 0.0, 1.0);
    float e01 = clamp(m / 0.34, 0.0, 1.0);
    float e2  = clamp((m - 0.33) / 0.34, 0.0, 1.0);
    float e3  = clamp((m - 0.66) / 0.34, 0.0, 1.0);
    vec4  ext = vec4(e01, e01, e2, e3);

    float t = iTime * speed;

    // Min distance to any of the 32 edges (vertices differing in one bit).
    float best = 1e9;
    for (int i = 0; i < 16; i++) {
        for (int d = 0; d < 4; d++) {
            if (((i >> d) & 1) == 1) continue;     // draw each edge once
            int j = i | (1 << d);
            vec2 pa = hcProject(hcVertex(i, ext), t) * scale;
            vec2 pb = hcProject(hcVertex(j, ext), t) * scale;
            best = min(best, segDist(uv, pa, pb));
        }
    }

    float lw = thickness * 0.004;
    float g  = smoothstep(lw, 0.0, best);          // line core + soft edge
    g += 0.4 * smoothstep(lw * 3.0, 0.0, best);    // faint glow
    g = clamp(g, 0.0, 1.0);

    vec3 col = hue2rgb(hue) * g;
    fragColor = vec4(col, g);
}
