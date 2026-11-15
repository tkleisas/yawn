// 32 · Mandala Seed — devotional radial iconography generated from a
// seed picture. The picture supplies the palette and continuous
// generative parameters (ring spacing, petal amplitude, ornament phase
// from fixed-point content probes) while the geometry is a mirrored
// N-fold mandala: lotus petal bands, dashed rings, seam spokes, a
// medallion and gold filigree over a lapis/parchment ground. The image
// is also sampled through the folded coordinates so the ornament picks
// up the picture's own material — never the picture itself.
// Kick blooms the petals; the beat turns the wheel. Deterministic:
// same picture + same knobs → same art. MIT License.

uniform float segments; // @range 4..16 default=9    fold count (rounded)
uniform float petals;   // @range 3..24 default=8    petals per ring
uniform float rings;    // @range 1..6  default=3    concentric bands
uniform float gold;     // @range 0..1  default=0.85 gold linework amount
uniform float ink;      // @range 0..1  default=0.55 engraving density
uniform float palette;  // @range 0..1  default=0.35 0=picture 1=traditional
uniform float spin;     // @range -2..2 default=0.12 rotation speed
uniform float glow;     // @range 0..1  default=0.50 halo + line glow
uniform float zoom;     // @range 0.5..1.4 default=1.0 wheel scale
uniform float seed;     // @range 0..1  default=0.0  alternate take offset

const float PI = 3.141592653589793;

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    uv /= max(zoom, 0.5);            // wheel scale (fit inside frames)
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    // ── seed picture probes: palette + continuous generative params ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hi  = max(max(avg.r, avg.g), avg.b);
    float lo  = min(min(avg.r, avg.g), avg.b);
    float chroma = hi - lo;
    float h = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                        vec3(12.9898, 78.233, 37.719)) * 43758.5453
                    + seed * 0.6180339);

    // Palette: picture-derived, blended toward the traditional thangka
    // scheme (deep lapis, parchment, gold, cinnabar) by the palette knob.
    vec3 imgDeep   = avg * 0.16 + vec3(0.012, 0.014, 0.028);
    vec3 imgPaper  = clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0);
    vec3 imgGold   = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 imgAccent = clamp(mix(s2, s4, 0.5) * 1.5 + 0.05, 0.0, 1.0);
    vec3 colDeep   = mix(imgDeep,   vec3(0.030, 0.045, 0.110), palette);
    vec3 colPaper  = mix(imgPaper,  vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold   = mix(imgGold,   vec3(0.880, 0.660, 0.250), palette);
    vec3 colAccent = mix(imgAccent, vec3(0.540, 0.130, 0.095), palette);

    // ── geometry: beat-locked turn + mirrored N-fold fold ──
    a += 0.31 + iTime * spin + iBeat * 0.02 + h * 6.28318;
    float folds = floor(segments + 0.5);
    float seg = PI / folds;
    float af = mod(a, 2.0 * seg);
    af = abs(af - seg);              // 0..seg inside one mirrored wedge

    // Continuous structure from the picture (no bucketed counts, so a
    // live video seed breathes instead of popping).
    float ringStep = mix(0.105, 0.150, clamp(lum, 0.0, 1.0));
    float ringBase = 0.150;
    float petalAmp = 0.018 + 0.030 * chroma + 0.012 * seed;
    float petalN   = floor(petals + 0.5);
    float cell     = af / seg;       // 0..1 across the wedge
    float kick     = clamp(iKick, 0.0, 1.0);
    // Geometry stays put (radius pulsing reads as jitter); the kick
    // drives glow, petal bloom and halo instead.
    float pulse    = 1.0 + 0.05 * kick;
    float rOuter   = (ringBase + (rings - 0.5) * ringStep) + 0.030;

    // The picture's material, sampled through the folded coordinates.
    vec3 imgFine = texture(iChannel2, vec2(cell * 0.55 + 0.22,
                                           r * 0.90 + 0.05)).rgb;

    // ── ground: lapis depth, picture wash, slow smoke, grain, halo ──
    vec3 col = colDeep * (0.50 + 0.50 * (1.0 - smoothstep(0.1, 1.0, r)));
    col += imgFine * 0.050 * (0.35 + 0.65 * (1.0 - smoothstep(0.0, 1.1, r)));
    float smoke = 0.5 + 0.5 * sin(r * 4.0 - iTime * 0.15
                    + sin(a * 2.0 + h * 6.28318) * 1.6);
    col += mix(colDeep, colPaper, 0.20) * smoke * 0.10 *
           (1.0 - smoothstep(0.25, 1.05, r));
    // Parchment grain: a stable per-pixel hash, kept very low.
    float grain = fract(sin(dot(floor(fragCoord), vec2(12.9898, 78.233)))
                        * 43758.5453);
    col += (grain - 0.5) * 0.020;
    col += colDeep * 0.55 * smoothstep(0.85, 0.0, r);
    float halo = exp(-r * 3.0) * (0.35 + glow * 0.65) * (0.85 + 0.5 * kick);
    col += colPaper * halo * 0.18;

    // ── ornament ──
    float goldMask = 0.0;
    float petalFill = 0.0;
    float accentHit = 0.0;
    float accentMask = 0.0;

    // Rings of dashes + lotus petals, out to the halo edge.
    for (int k = 0; k < 6; ++k) {
        if (float(k) >= rings + 0.5) break;
        float rr = (ringBase + float(k) * ringStep) * pulse;
        float w  = 0.0055 + 0.0035 * float(k);

        // Dashed ring — dash cells align with the petal lattice.
        float dashPhase = cell * PI * petalN * 2.0 + float(k) * 1.7
                        + h * 6.28318;
        float dash = smoothstep(0.10, 0.55, 0.5 + 0.5 * sin(dashPhase));
        goldMask = max(goldMask,
            (1.0 - smoothstep(w, w * 2.2, abs(r - rr))) * dash);

        // Lotus petal: a teardrop arcing outward from this ring.
        float petalTop = rr + petalAmp * sin(cell * PI);
        float petalEdge = 1.0 - smoothstep(0.0020, 0.0060,
                                           abs(r - petalTop));
        float inPetal = step(rr + 0.003, r) * step(r, petalTop);
        goldMask = max(goldMask, petalEdge);
        petalFill = max(petalFill, inPetal * (0.35 + 0.65 * cell));
        accentHit = max(accentHit,
            inPetal * smoothstep(0.2, 0.9, sin(cell * PI * petalN)));

        // Bead at the wedge centre on this ring (cinnabar-set).
        vec2 dq = vec2((cell - 0.5) * seg * r, r - rr + petalAmp * 0.5);
        float bead = 1.0 - smoothstep(0.0045, 0.0090, length(dq));
        accentMask = max(accentMask, bead * (0.55 + 0.45 * float(k % 2)));

        // Seam spokes (wedge borders) — inside the wheel only, and a
        // fainter mid-wedge ray band past the last ring.
        float seam = min(r * af, r * (seg - af));
        float rimFade = 1.0 - smoothstep(rOuter - 0.01, rOuter + 0.05, r);
        goldMask = max(goldMask,
            (1.0 - smoothstep(0.0030, 0.0070, seam)) * rimFade);
        float ray = abs((cell - 0.5) * seg) * r;
        float rayBand = smoothstep(rOuter, rOuter + 0.02, r) *
                        (1.0 - smoothstep(0.62, 0.74, r));
        goldMask = max(goldMask,
            (1.0 - smoothstep(0.0022, 0.0055, ray)) * rayBand * 0.55);
    }

    // Medallion: image-tinted core, double ring, radial ticks, red rim.
    float mR = 0.095 * pulse;
    float disc = 1.0 - smoothstep(mR, mR + 0.006, r);
    col = mix(col, mix(colPaper, imgFine, 0.45) * (0.55 + 0.45 * lum),
              disc * 0.85);
    goldMask = max(goldMask,
        1.0 - smoothstep(0.0035, 0.0075, abs(r - mR)));
    goldMask = max(goldMask,
        1.0 - smoothstep(0.0030, 0.0065, abs(r - mR * 0.55)));
    float ticks = abs(sin(cell * PI * petalN * 0.5));
    goldMask = max(goldMask,
        (1.0 - smoothstep(0.0025, 0.0060, abs(r - mR * 0.8))) *
        step(0.92, ticks));
    accentMask = max(accentMask,
        1.0 - smoothstep(0.0030, 0.0065, abs(r - mR * 1.12)));
    accentMask = max(accentMask,
        1.0 - smoothstep(0.0060, 0.0110, r));   // bindu

    // Fine engraving: concentric hairlines warped by the petal lattice.
    float engrave = pow(0.5 + 0.5 * sin(r * 150.0
                        + sin(cell * PI * petalN * 3.0 + h * 9.0) * 3.0),
                        28.0);
    engrave *= ink * (1.0 - smoothstep(0.55, 0.95, r)) * step(mR, r);

    // ── compose ──
    col = mix(col, mix(colPaper, imgFine, 0.65) * (0.45 + 0.55 * cell),
              petalFill * 0.35);
    col += mix(colPaper, imgFine, 0.6) * petalFill * 0.06;   // petal bloom
    col = mix(col, colAccent, accentHit * 0.12 * (0.6 + 0.4 * kick));
    col = mix(col, colAccent, accentMask * (0.55 + 0.45 * kick) * 0.85);
    col = mix(col, colGold, goldMask * gold * (0.85 + 0.35 * kick));
    col += colGold * goldMask * glow * 0.25 * (0.6 + 0.8 * kick);
    col = mix(col, mix(colGold, imgFine, 0.35), engrave * gold * 0.35);

    // Soft outer darkness so the wheel sits in a pool of light.
    col *= 1.0 - smoothstep(0.85, 1.35, r) * 0.85;

    fragColor = vec4(col, 1.0);
}
