// 40 · Sri Yantra — the classic nine-triangle sacred diagram, generated
// deterministically: four upward and five downward interlocking
// triangles around a bindu, ringed by the 8- and 16-petal lotuses and
// the square bhupura with its four gates. Gold linework on deep lapis
// (palette 0 = picture-derived accents from the seed image on iChannel2,
// 1 = traditional). Kick brightens the lines and the bindu.
// MIT License.

uniform float size;    // @range 0.5..1.3 default=0.86 diagram scale
uniform float gold;    // @range 0..1  default=0.90 linework strength
uniform float palette; // @range 0..1  default=0.45 0=picture 1=traditional
uniform float glow;    // @range 0..1  default=0.55
uniform float spin;    // @range -1..1 default=0.02 slow turn
uniform float kick;    // @range 0..1  default=0.35
uniform float seed;    // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

// Triangle edge field: min distance to the three sides of an isosceles
// triangle with apex (0, ay) and base corners (±w, by).
float triEdges(vec2 p, float ay, float by, float w) {
    float d = segDist(p, vec2(0.0, ay), vec2(-w, by));
    d = min(d, segDist(p, vec2(0.0, ay), vec2(w, by)));
    d = min(d, segDist(p, vec2(-w, by), vec2(w, by)));
    return d;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);
    float a = atan(p.y, p.x);

    // ── seed picture probes: palette + phase (family convention) ──
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

    vec3 imgGold  = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 colDeep  = mix(avg * 0.16 + vec3(0.012, 0.014, 0.028),
                        vec3(0.030, 0.045, 0.110), palette);
    vec3 colPaper = mix(clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0),
                        vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold  = mix(imgGold, vec3(0.880, 0.660, 0.250), palette);
    vec3 colAcc   = mix(clamp(mix(s2, s4, 0.5) * 1.5 + 0.05, 0.0, 1.0),
                        vec3(0.560, 0.130, 0.090), palette);

    float k = clamp(iKick, 0.0, 1.0) * kick;
    vec2 q = p / size;
    q = mat2(cos(iTime * spin), -sin(iTime * spin),
             sin(iTime * spin),  cos(iTime * spin)) * q;
    float qr = length(q);
    float th = atan(q.y, q.x);

    // ── ground ──
    vec3 col = colDeep * (0.55 + 0.45 * (1.0 - smoothstep(0.1, 1.0, r)));
    col += colDeep * 0.6 * smoothstep(0.9, 0.0, r);
    float halo = exp(-r * 3.0) * (0.35 + glow * 0.65) * (0.85 + 0.5 * k);
    col += colPaper * halo * 0.10;

    float goldMask = 0.0;
    float accentMask = 0.0;

    // ── the nine triangles (four up, five down) ──
    // Heights follow the classical layout's feel: a dense stack of
    // interlocking lines crossing near the bindu.
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        float ay = 0.34 - 0.11 * fi;
        float by = -0.06 - 0.10 * fi;
        float w  = 0.50 - 0.115 * fi;
        goldMask = max(goldMask,
            1.0 - smoothstep(0.0020, 0.0060, triEdges(q, ay, by, w)));
    }
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float ay = -0.42 + 0.105 * fi;   // apex below
        float by = 0.06 + 0.095 * fi;    // base above
        float w  = 0.55 - 0.105 * fi;
        // downward triangle = upward with y negated
        goldMask = max(goldMask, 1.0 - smoothstep(0.0020, 0.0060,
            triEdges(vec2(q.x, -q.y), -ay, -by, w)));
    }

    // ── bindu ──
    float bindu = 1.0 - smoothstep(0.010, 0.016, length(q));
    accentMask = max(accentMask, 1.0 - smoothstep(0.007, 0.012, length(q)));
    goldMask = max(goldMask, bindu);

    // ── 8-petal and 16-petal lotus rings ──
    for (int ring = 0; ring < 2; ++ring) {
        float rl = ring == 0 ? 0.40 : 0.47;
        float petals = ring == 0 ? 8.0 : 16.0;
        // Petal tips: a scalloped edge around rl.
        float edge = rl + 0.045 * abs(sin(th * petals * 0.5 + hh * TAU));
        goldMask = max(goldMask,
            1.0 - smoothstep(0.0020, 0.0055, abs(qr - edge)));
        // Petal separators: short radial ticks at the scallop seams.
        float seam = abs(sin(th * petals * 0.5 + hh * TAU));
        goldMask = max(goldMask,
            (1.0 - smoothstep(0.0018, 0.0050, abs(qr - rl - 0.020)))
            * smoothstep(0.92, 0.99, seam));
    }

    // ── square bhupura with gates ──
    vec2 aq = abs(q);
    float sq = max(aq.x, aq.y);
    float border = 1.0 - smoothstep(0.0020, 0.0055, abs(sq - 0.530));
    float border2 = 1.0 - smoothstep(0.0020, 0.0055, abs(sq - 0.560));
    float gates = 0.0;
    for (int i = 0; i < 4; ++i) {
        float ang = float(i) * PI * 0.5;
        vec2 dir = vec2(cos(ang), sin(ang));
        vec2 c = dir * 0.530;
        vec2 t = vec2(-dir.y, dir.x);
        vec2 d = abs(vec2(dot(q - c, dir), dot(q - c, t)));
        gates = max(gates, 1.0 - smoothstep(0.0020, 0.0050,
                    max(d.x - 0.045, d.y - 0.045)));
    }
    goldMask = max(goldMask, max(border, border2));
    goldMask = max(goldMask, gates);

    // ── compose ──
    col = mix(col, colGold, goldMask * gold * (0.85 + 0.35 * k));
    col += colGold * goldMask * glow * 0.22 * (0.6 + 0.9 * k);
    col = mix(col, colAcc, accentMask * 0.9);
    // faint inner fills so the triangles read as planes, not wireframe
    float inner = smoothstep(0.62, 0.0, qr) * 0.10;
    col += mix(colPaper, colGold, 0.5) * inner * (0.4 + 0.3 * gold);

    col += (fract(sin(dot(fragCoord, vec2(12.9898, 78.233))) * 43758.5453)
            - 0.5) * 0.015;
    col *= 1.0 - smoothstep(0.75, 1.3, r) * 0.5;

    fragColor = vec4(col, 1.0);
}
