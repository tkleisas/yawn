// 33 · Thangka Frame — an alpha ornament layer: brocade border, gold
// piping, lotus motif band, corner gems and an inward halo, sized as a
// frame around the screen. Designed to composite over the mandala (or
// any other source) with the normal blend — the centre aperture is
// transparent, so the layer below stays visible. Like 32_mandala_seed,
// the palette and motif phase come from the clip's seed picture on
// iChannel2 (palette knob: 0 = picture-derived, 1 = traditional
// lapis/vermilion/parchment/gold). Kick brightens the gold and flares
// the corner gems. Output is premultiplied alpha, per the engine's
// layer convention. Deterministic per picture + knobs. MIT License.

uniform float band;    // @range 0.02..0.16 default=0.09 border thickness
uniform float weave;   // @range 0..1  default=0.60 brocade weave
uniform float motifs;  // @range 4..32 default=14   motif repeats
uniform float gold;    // @range 0..1  default=0.85 gold linework
uniform float palette; // @range 0..1  default=0.45 0=picture 1=traditional
uniform float glow;    // @range 0..1  default=0.50 inward halo
uniform float gems;    // @range 0..1  default=0.70 corner gems
uniform float seed;    // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;

// Antialiased coverage of [lo, hi] in `e`.
float bandMask(float e, float lo, float hi, float aa) {
    return smoothstep(lo - aa, lo + aa, e) *
           (1.0 - smoothstep(hi - aa, hi + aa, e));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float aspect = res.x / res.y;
    vec2 halfExt = vec2(0.5 * aspect, 0.5);

    // ── seed picture probes: palette + pattern phase ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hi = max(max(avg.r, avg.g), avg.b);
    float lo = min(min(avg.r, avg.g), avg.b);
    float chroma = hi - lo;
    float h = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                        vec3(12.9898, 78.233, 37.719)) * 43758.5453
                    + seed * 0.6180339);

    // Palette — same convention as the mandala.
    vec3 imgDeep   = avg * 0.16 + vec3(0.012, 0.014, 0.028);
    vec3 imgPaper  = clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0);
    vec3 imgGold   = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 imgAccent = clamp(mix(s2, s4, 0.5) * 1.5 + 0.05, 0.0, 1.0);
    vec3 colDeep   = mix(imgDeep,   vec3(0.030, 0.045, 0.110), palette);
    vec3 colPaper  = mix(imgPaper,  vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold   = mix(imgGold,   vec3(0.880, 0.660, 0.250), palette);
    vec3 colAccent = mix(imgAccent, vec3(0.540, 0.130, 0.095), palette);

    // ── frame distance (uniform width in aspect-correct units) ──
    float ex = halfExt.x - abs(p.x);
    float ey = halfExt.y - abs(p.y);
    bool sideEdge = ex < ey;
    float edge = min(ex, ey);                 // 0 at the screen border
    float along = sideEdge ? p.y / halfExt.y : p.x / halfExt.x;   // -1..1
    float aa = max(fwidth(edge), 0.0008);
    float b = max(band, 0.005);
    float kick = clamp(iKick, 0.0, 1.0);
    // Motif lattice phase wobbles from the picture + seed.
    float phase = h * 6.28318 + seed * 3.0;
    float lat = along * PI * motifs * 0.5 + phase;

    vec3 col = vec3(0.0);
    float alpha = 0.0;

    // ── outer piping ──
    float rimMask = bandMask(edge, 0.0, b * 0.14, aa);
    float rimLine = bandMask(edge, b * 0.115, b * 0.155, aa * 1.4);
    alpha = max(alpha, rimMask);
    col += mix(colDeep, colGold, 0.35 + 0.5 * rimLine) *
           (rimMask * (0.55 + 0.45 * rimLine));

    // ── brocade band: woven diagonal + diamond stitches ──
    float bLo = b * 0.16, bHi = b * 0.72;
    float brocadeMask = bandMask(edge, bLo, bHi, aa);
    if (brocadeMask > 0.001) {
        float u = (edge - bLo) / (bHi - bLo);          // 0..1 across
        // Fine cross-hatch: two diagonal thread systems + stitch beads.
        float warp = 0.5 + 0.5 * sin((u * 90.0 + lat * 1.35) * PI);
        float weft = 0.5 + 0.5 * sin((u * 90.0 - lat * 1.35) * PI);
        float thread = max(pow(warp, 12.0), pow(weft, 12.0));
        float stitch = pow(0.5 + 0.5 * sin(lat * 2.0 + u * 6.0), 10.0);
        vec3 cloth = mix(colDeep, colAccent, 0.30 + 0.45 * palette);
        cloth *= 0.82 + 0.18 * thread;
        cloth = mix(cloth, colPaper, thread * weave * 0.10);
        cloth += colGold * stitch * weave * 0.30;
        col += cloth * brocadeMask;
        alpha = max(alpha, brocadeMask);
    }

    // ── gold separators ──
    float sep1 = bandMask(edge, b * 0.72, b * 0.755, aa * 1.4);
    float sep2 = bandMask(edge, b * 0.865, b * 0.90, aa * 1.4);
    col += colGold * (sep1 + sep2) * gold;
    alpha = max(alpha, max(sep1, sep2) * gold);

    // ── lotus-motif band: ticks, dots, little arcs ──
    float mLo = b * 0.775, mHi = b * 0.855;
    float motifMask = bandMask(edge, mLo, mHi, aa);
    if (motifMask > 0.001) {
        float m = abs(sin(lat));
        float tick = pow(1.0 - m, 10.0);               // thin points
        float dot = 1.0 - smoothstep(0.10, 0.26, m);   // round beads
        float v = (edge - mLo) / (mHi - mLo);
        float arc = pow(1.0 - abs(v - 0.5) * 2.0, 3.0) * tick;
        vec3 motifCol = mix(colGold, colAccent, 0.35 * (0.5 + 0.5 * sin(lat)));
        col += motifCol * (arc * 0.9 + dot * 0.55 + tick * 0.25) * motifMask;
        alpha = max(alpha, motifMask * (0.35 + 0.65 * dot));
    }

    // ── inner piping ──
    float innerLine = bandMask(edge, b * 0.905, b * 0.935, aa * 1.4);
    col += colGold * innerLine * gold * (0.9 + 0.4 * kick);
    alpha = max(alpha, innerLine * gold);

    // ── inward halo: a warm bleed into the aperture ──
    float inside = smoothstep(b * 0.94, b * 1.06, edge);
    float bleed = exp(-(edge - b) * 26.0) * inside;
    col += mix(colGold, colAccent, 0.25) * bleed * glow *
           (0.45 + 0.55 * kick) * 0.55;
    alpha = max(alpha, clamp(bleed * glow * 0.45, 0.0, 1.0));

    // ── corner gems: lozenge + rings, cinnabar-set ──
    // Inset the gem centres onto the brocade band instead of the exact
    // screen corner, where half of the lozenge would be off-screen.
    vec2 cp = abs(p) - (halfExt - vec2(0.5 * b));  // 0 at gem centres
    float dCorner = length(cp);
    float gemR = 0.055 * (0.55 + 0.45 * gems) * (1.0 + 0.10 * kick);
    float lz = abs(cp.x) + abs(cp.y);              // diamond metric
    float gem = 1.0 - smoothstep(gemR * 0.75, gemR * 0.75 + aa, lz);
    float gemRing = bandMask(dCorner, gemR * 0.62, gemR * 0.72, aa * 1.5);
    float gemCore = 1.0 - smoothstep(0.0, gemR * 0.30, dCorner);
    if (gems > 0.001) {
        vec3 gemCol = mix(colAccent, colGold, 0.25 + 0.25 * kick);
        col += gemCol * (gemCore * 1.2 + gemRing * gold) *
               (0.55 + 0.45 * kick) * gems;
        col += colGold * gem * 0.35 * gems;
        alpha = max(alpha, max(gemCore, gemRing) * gems);
        alpha = max(alpha, gem * 0.55 * gems);
    }

    // Soften alpha at the very screen edge so the frame blends into the
    // output window's black rather than ending in a hard cut.
    float screenEdge = min(min(fragCoord.x, res.x - fragCoord.x),
                           min(fragCoord.y, res.y - fragCoord.y));
    alpha *= smoothstep(0.0, 2.0, screenEdge);

    alpha = clamp(alpha, 0.0, 1.0);
    // Premultiplied output (engine layer convention — text shaders do the
    // same: vec4(col * coverage, coverage)).
    fragColor = vec4(col * alpha, alpha);
}
