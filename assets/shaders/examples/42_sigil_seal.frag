// 42 · Sigil Seal — a talisman of procedural sigils: every cell carves a
// small glyph from the picture hash (2–4 strokes on a 3×3 lattice), laid
// either as a grid or as a radial seal with a vermilion centre. Palette
// 0 = picture-tinted ink on parchment, 1 = gold engraved on lapis.
// Deterministic per seed + knobs. MIT License.

uniform float cells;   // @range 3..9   default=5    grid size (grid mode)
uniform float radial;  // @range 0..1   default=0.0  0=grid 1=radial seal
uniform float ink;     // @range 0..1   default=0.85  stroke strength
uniform float palette; // @range 0..1   default=0.25  0=parchment 1=lapis
uniform float glow;    // @range 0..1   default=0.35
uniform float kick;    // @range 0..1   default=0.30
uniform float seed;    // @range 0..1   default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

// One procedural glyph in local coords q (roughly -0.5..0.5): a vertical
// stem plus two or three strokes between lattice points.
float glyph(vec2 q, float h) {
    float d = 1e9;
    // stem, slightly off-centre and jittered
    float sx = (hash11(h * 3.1) - 0.5) * 0.14;
    d = min(d, segDist(q, vec2(sx, -0.32), vec2(sx, 0.32)));
    int strokes = 2 + int(hash11(h * 7.7) * 2.0);   // 2..3 extra strokes
    for (int i = 0; i < 3; ++i) {
        if (i >= strokes) break;
        float a = hash11(h * 11.3 + float(i) * 2.7);
        float b = hash11(h * 5.9 + float(i) * 4.1 + 3.0);
        float c = hash11(h * 7.3 + float(i) * 6.7 + 9.0);
        float e = hash11(h * 2.9 + float(i) * 8.3 + 5.0);
        vec2 pa = vec2(-0.32 + 0.64 * a, -0.32 + 0.64 * b);
        vec2 pb = vec2(-0.32 + 0.64 * c, -0.32 + 0.64 * e);
        // horizontal bias: snap one coordinate for calligraphic strokes
        if (hash11(h * 13.1 + float(i)) > 0.5) pb.y = pa.y;
        else                                   pb.x = pa.x;
        d = min(d, segDist(q, pa, pb));
    }
    return d;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);

    // ── seed probes: palette + glyph entropy ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hh = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    // parchment ↔ lapis ground; ink ↔ gold strokes.
    vec3 paper = mix(clamp(avg * 0.75 + lum * 0.45 + 0.18, 0.0, 1.0),
                     vec3(0.910, 0.840, 0.650), 0.3 + 0.7 * (1.0 - palette));
    vec3 ground = mix(vec3(0.16, 0.10, 0.06) * (0.5 + 0.5 * lum),
                      vec3(0.030, 0.045, 0.110), palette);
    vec3 inkCol = mix(vec3(0.06, 0.045, 0.035), paper,
                      smoothstep(0.0, 1.0, 1.0 - palette) * 0.0);
    inkCol = mix(vec3(0.060, 0.045, 0.035),
                 vec3(0.880, 0.660, 0.250), palette);
    vec3 accent = mix(vec3(0.560, 0.130, 0.090),
                      clamp(mix(s2, s4, 0.5) * 1.4 + 0.06, 0.0, 1.0),
                      1.0 - palette);

    vec3 col = ground;
    // paper grain + vignette
    col += (hash21(fragCoord + seed * 7.0) - 0.5) * 0.02;
    col *= 1.0 - smoothstep(0.6, 1.2, r) * 0.4;

    float k = clamp(iKick, 0.0, 1.0) * kick;
    float aa = 1.6 / res.y;
    float strokes = 0.0;
    float redMask = 0.0;

    if (radial < 0.5) {
        // ── grid of sigils ──
        float n = floor(cells + 0.5);
        vec2 gp = (p + vec2(0.5 * 0.888, 0.5)) * vec2(0.888 / (n * 0.5), 1.0)
                * n * 0.5;   // cell space across the frame
        vec2 cell = floor(gp);
        vec2 q = fract(gp) - 0.5;
        if (cell.x >= 0.0 && cell.y >= 0.0 && cell.x < n && cell.y < n) {
            float h = hash21(cell + vec2(hh * 37.0, seed * 13.0));
            float d = glyph(q, h);
            strokes = 1.0 - smoothstep(0.010, 0.010 + aa * 2.0, d);
            // tiny carved dot at the glyph foot
            strokes = max(strokes,
                1.0 - smoothstep(0.008, 0.008 + aa * 2.0,
                                 length(q - vec2(0.30, -0.30))));
        }
    } else {
        // ── radial seal: sigils around a vermilion centre ──
        float petals = 10.0;
        float ang = atan(p.y, p.x);
        float ringR = 0.34;
        float ai = floor(ang / TAU * petals + 0.5);
        float a0 = ai / petals * TAU + hh * TAU;
        vec2 c = vec2(cos(a0), sin(a0)) * ringR;
        vec2 t = vec2(-sin(a0), cos(a0));
        vec2 q = vec2(dot(p - c, t), dot(p - c, vec2(cos(a0), sin(a0))))
               / 0.13;
        float h = hash11(ai * 3.7 + hh * 11.0 + seed * 5.0);
        strokes = 1.0 - smoothstep(0.030, 0.030 + aa * 2.0,
                                   glyph(q * 0.9, h));
        // seal rings
        strokes = max(strokes,
            1.0 - smoothstep(0.0020, 0.0020 + aa * 1.5,
                             abs(length(p) - 0.20)));
        strokes = max(strokes,
            1.0 - smoothstep(0.0020, 0.0020 + aa * 1.5,
                             abs(length(p) - 0.46)));
        // vermilion heart with a carved mark
        float heart = 1.0 - smoothstep(0.11, 0.115, length(p));
        redMask = heart * (1.0 - smoothstep(0.02, 0.03,
                 glyph(p / 0.11, hh * 9.1)));
    }

    col = mix(col, paper, 0.16 * (1.0 - palette));   // paper lift
    col = mix(col, inkCol, strokes * ink * (0.85 + 0.35 * k));
    col += inkCol * strokes * glow * 0.25 * (0.6 + 0.8 * k);
    col = mix(col, accent, redMask * 0.95);

    fragColor = vec4(col, 1.0);
}
