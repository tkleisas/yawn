// 36 · Ink Brush — sumi-e ink overlay: a rough enso circle with dry-brush
// breakup and ink pooling, quick calligraphic arc flicks, splatter dots
// that bloom on kicks, and a vermilion seal with a carved glyph. An
// alpha layer (premultiplied) that composites over the rest of the
// family — normal or multiply reads like ink on the mandala; screen
// gives a ghost. Palette knob: 0 = ink tinted from the seed picture,
// 1 = traditional near-black sumi. Deterministic per picture + knobs.
// MIT License.

uniform float enso;    // @range 0..0.6 default=0.30 enso radius (0=off)
uniform float grain;   // @range 0..1  default=0.60 dry-brush breakup
uniform float strokes; // @range 0..6  default=3    calligraphic flicks
uniform float splatter;// @range 0..1  default=0.50 ink splatter amount
uniform float seal;    // @range 0..1  default=0.70 vermilion seal
uniform float palette; // @range 0..1  default=0.40 0=picture 1=ink
uniform float spin;    // @range -1..1 default=0.04 slow field rotation
uniform float seed;    // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x),
               f.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;

    // ── seed picture probes: palette + stroke phase ──
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

    // Ink: picture-tinted dark, blended toward sumi near-black.
    vec3 imgInk = clamp(avg * 0.16 + vec3(0.010, 0.010, 0.014), 0.0, 1.0);
    vec3 inkCol = mix(imgInk, vec3(0.018, 0.018, 0.026), 0.45 + 0.55 * palette);
    vec3 colAccent = mix(clamp(mix(s2, s4, 0.5) * 1.5 + 0.08, 0.0, 1.0),
                         vec3(0.560, 0.120, 0.080), palette);
    vec3 colPaper = mix(clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0),
                        vec3(0.910, 0.840, 0.650), palette);

    float kick = clamp(iKick, 0.0, 1.0);
    float aa = 1.5 / res.y;          // ~1.5 px in aspect units

    vec2 p0 = p;   // unrotated — the seal is a fixed chop, not part of the swirl

    // Slow field rotation (beat-locked drift).
    float rot = iTime * spin + iBeat * 0.008 + hh * TAU * 0.18;
    float cr = cos(rot), sr = sin(rot);
    p = mat2(cr, -sr, sr, cr) * p;

    float r = length(p);
    float a = atan(p.y, p.x);

    vec3 col = vec3(0.0);
    float alpha = 0.0;

    // ── enso: one rough brush circle with a gap and a heavy start ──
    if (enso > 0.001) {
        float R = enso * (0.96 + 0.08 * hash11(hh * 7.0 + 2.0));
        float a0 = hh * TAU;
        float coverage = 0.78 + 0.14 * hash11(hh * 3.0 + 1.0);
        float rel = mod(a - a0 + TAU, TAU);
        float along = rel / TAU;     // 0..1 around the ring

        // Brush radius wobbles with a low-frequency hand tremor + press.
        float wob = (vnoise2(vec2(rel * 3.0, hh * 9.0)) - 0.5) * 0.016;
        float press = 0.5 + 0.5 * sin(PI * clamp(along / coverage, 0.0, 1.0));
        float w = (0.026 + 0.012 * hash11(hh * 5.0 + 4.0))
                * (0.45 + 0.75 * press) * (1.0 + 0.22 * kick);

        float d = abs(r - R - wob) - w * 0.5;
        float cov = 1.0 - smoothstep(0.0, aa * 1.4, d);
        cov *= step(rel, coverage * TAU);
        // Ink pooling at the brush-down end.
        cov = max(cov, (1.0 - smoothstep(0.0, aa * 1.4,
                        abs(r - R - wob) - w * 0.9))
                        * (1.0 - smoothstep(0.0, 0.10, rel)));

        // Dry-brush breakup: hairs run RADIALLY (low frequency along the
        // stroke, high across it) so the stroke streaks, not speckles.
        float dry = vnoise2(vec2(rel * 7.0, (r - R) * 190.0 + hh * 4.0));
        float dryMask = smoothstep(0.26 - grain * 0.20, 0.58, dry + 0.22 * grain);
        cov *= mix(1.0, dryMask, grain);

        // Taper the far end out of the stroke.
        cov *= smoothstep(0.0, 0.06, rel)
             * (1.0 - smoothstep(coverage * TAU - 0.30, coverage * TAU, rel));

        // Ink bleed into the paper — gated to the stroke span, or the halo
        // bridges the enso gap as a grey veil.
        float bleed = exp(-max(abs(r - R) - w, 0.0) * 70.0) * 0.18
                    * (0.6 + 0.4 * kick)
                    * step(rel, coverage * TAU)
                    * smoothstep(0.0, 0.12, rel);

        col += inkCol * (cov + bleed);
        alpha = max(alpha, cov + bleed * 0.5);
    }

    // ── calligraphic arc flicks ──
    for (int j = 0; j < 6; ++j) {
        if (float(j) >= strokes) break;
        float hj = hash11(float(j) * 3.17 + hh * 5.0);
        float Rj = enso > 0.001 ? enso * (0.42 + 0.72 * hj) : 0.30 * hj;
        float aj0 = hash11(float(j) * 7.31 + hh * 2.0) * TAU;
        float span = (0.14 + 0.26 * hash11(float(j) * 1.77 + 3.0)) * TAU;
        float drift = (hash11(float(j) * 5.1 + 1.0) - 0.5) * 0.10;
        float relj = mod(a - aj0 + TAU, TAU);
        if (relj < span) {
            float t = relj / span;
            float rr = Rj + drift * t;          // slight spiral drift
            float wj = 0.010 * pow(sin(PI * t), 0.7)
                     * (0.55 + 0.45 * hash11(float(j) * 9.7 + hh))
                     * (1.0 + 0.35 * kick);
            float d = abs(r - rr) - wj * 0.5;
            float cov = 1.0 - smoothstep(0.0, aa * 1.2, d);
            float dry = vnoise2(vec2(relj * 9.0, r * 210.0 + float(j) * 5.0));
            cov *= mix(1.0, smoothstep(0.28 - grain * 0.15, 0.60, dry + 0.2 * grain),
                       grain * 0.8);
            col += inkCol * cov;
            alpha = max(alpha, cov);
        }
    }

    // ── splatter: round drops + short smears, blooming on kicks ──
    for (int j = 0; j < 12; ++j) {
        if (float(j) >= splatter * 12.0) break;
        float hj = hash11(float(j) * 4.13 + hh * 6.0);
        float hj2 = hash11(float(j) * 8.71 + hh * 3.0);
        vec2 sp = vec2((hj - 0.5) * 1.55, (hj2 - 0.5) * 0.95);
        float bloom = step(0.55, hash11(float(j) * 2.37 + hh))
                    * (0.5 + 0.9 * kick);
        float rj = (0.0022 + 0.0075 * hj2 * hj2) * (1.0 + bloom);
        vec2 q = p - sp;
        // Half the drops smear into a short diagonal tick.
        float smear = step(0.5, hj);
        vec2 dir = normalize(vec2(0.7, 0.7));
        vec2 q2 = vec2(dot(q, dir), dot(q, vec2(-0.7, 0.7)));
        q2.x /= (1.0 + 3.0 * smear);
        float d = length(mix(q, q2, smear));
        float cov = 1.0 - smoothstep(rj, rj + aa, d);
        col += inkCol * cov;
        alpha = max(alpha, cov);
    }

    // ── vermilion seal with a carved glyph ──
    if (seal > 0.001) {
        float sSz = 0.055 + 0.022 * seal;
        vec2 sp = vec2(0.60, -0.34);
        float sa = (hash11(hh * 4.0 + 6.0) - 0.5) * 0.18;
        float cs = cos(sa), sn = sin(sa);
        vec2 q = mat2(cs, -sn, sn, cs) * (p0 - sp);
        float boxD = max(abs(q.x), abs(q.y)) - sSz * 0.5;
        float sealCov = 1.0 - smoothstep(0.0, aa, boxD);

        // Carved glyph: three horizontal bars + a stem (a 王-shaped
        // chop), bar heights jittered by the picture hash. AA is measured
        // in seal-local units (gq spans -1..1, so one pixel is
        // (~1.5/res.y)/(sSz*0.5) — NOT aa*90/sSz, which was 5x too wide).
        vec2 gq = q / (sSz * 0.5);            // -1..1 in the seal
        float gAa = aa / (sSz * 0.5);
        float g = 0.0;
        for (int b = 0; b < 3; ++b) {
            float yj = -0.42 + float(b) * 0.42
                     + (hash11(hh * 9.0 + float(b) * 3.1) - 0.5) * 0.10;
            float halfW = 0.24 + 0.14 * (b == 1 ? 1.0 : 0.0);
            g = max(g,
                (1.0 - smoothstep(0.050 - gAa, 0.050 + gAa,
                                  abs(gq.y - yj))) *
                (1.0 - smoothstep(halfW - gAa, halfW + gAa,
                                  abs(gq.x))));
        }
        g = max(g,
            (1.0 - smoothstep(0.045 - gAa, 0.045 + gAa, abs(gq.x))) *
            (1.0 - smoothstep(0.56 - gAa, 0.56 + gAa, abs(gq.y))));

        // The carved strokes knock the seal out to true transparency
        // (an overlay has no paper to show through).
        float sealA = sealCov * (1.0 - g);
        col = mix(col, colAccent * (0.85 + 0.25 * kick), sealA);
        alpha = max(alpha, sealA);
    }

    alpha = clamp(alpha, 0.0, 1.0);
    // Premultiplied output (engine layer convention).
    fragColor = vec4(col * alpha, alpha);
}
