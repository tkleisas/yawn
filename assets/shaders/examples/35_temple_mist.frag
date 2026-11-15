// 35 · Temple Mist — an ink-wash eastern landscape: layered ridgelines
// with tiered pagoda temples, drifting mist bands, lanterns that flare on
// kicks, a pale moon disc with an enso ring, brush-wash sky and a few
// distant birds. The seed picture supplies the palette (palette knob:
// 0 = picture-derived, 1 = traditional lapis/ink/parchment/vermilion)
// and the skyline's phase; every shape is deterministic from the picture
// hash + seed. Audio: low band thickens the mist, kicks flare lanterns.
// Opaque output. MIT License.

uniform float layers;   // @range 2..6  default=4    ridgeline layers
uniform float pagodas;  // @range 0..6  default=3    temples on the ridges
uniform float peaks;    // @range 0.2..1 default=0.6 ridge jaggedness
uniform float mist;     // @range 0..1  default=0.55 haze density
uniform float lanterns; // @range 0..1  default=0.65 lantern glow
uniform float brush;    // @range 0..1  default=0.50 sky brush washes
uniform float sun;      // @range 0..1  default=0.60 moon disc + enso
uniform float palette;  // @range 0..1  default=0.45 0=picture 1=traditional
uniform float drift;    // @range -1..1 default=0.05 parallax drift
uniform float seed;     // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// 1D value noise (skyline silhouettes).
float vnoise1(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

// 2D value noise (sky washes), two octaves at the call site.
float vnoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x),
               f.y);
}

// Height of a tiered pagoda above its ridge, as a function of dx from
// the temple centre. Four tiers, upturned eaves, finial spike.
float pagodaH(float dx, float w) {
    float total = 0.0;
    float base = 0.0;
    float ww = w;
    for (int i = 0; i < 4; ++i) {
        float a = abs(dx) / max(ww, 1e-4);
        // body block between roofs
        if (a < 0.55) total = max(total, base + ww * 0.42);
        // tier roof: gable rising to the centre, eaves curling up at the tips
        float rh = ww * 0.62;
        float roof = base + ww * 0.42 + rh * (1.0 - a) + rh * 0.95 * a * a;
        if (a < 1.0) total = max(total, roof);
        base += ww * 0.42 + rh * 0.85;
        ww *= 0.76;
    }
    // finial — only above the tower body (the whole term must be gated;
    // adding the bare `base` would lift the ridge everywhere)
    total = max(total, (base + w * 0.22) * step(abs(dx), 0.045 * w));
    return total;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float h = 0.0;

    // ── seed picture probes: palette + skyline phase ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hi = max(max(avg.r, avg.g), avg.b);
    float lo = min(min(avg.r, avg.g), avg.b);
    float hh = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    vec3 imgDeep   = avg * 0.16 + vec3(0.012, 0.014, 0.028);
    vec3 imgPaper  = clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0);
    vec3 imgGold   = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 imgAccent = clamp(mix(s2, s4, 0.5) * 1.5 + 0.05, 0.0, 1.0);
    vec3 colDeep   = mix(imgDeep,   vec3(0.030, 0.045, 0.110), palette);
    vec3 colPaper  = mix(imgPaper,  vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold   = mix(imgGold,   vec3(0.880, 0.660, 0.250), palette);
    vec3 colAccent = mix(imgAccent, vec3(0.540, 0.130, 0.095), palette);

    float kick = clamp(iKick, 0.0, 1.0);
    float hdrift = iTime * drift;
    float mistAmt = mist * (0.70 + 0.55 * clamp(iAudioLow, 0.0, 1.0));
    float nLayers = floor(layers + 0.5);
    float nPag = floor(pagodas + 0.5);

    // ── sky: gradient, brush washes, ink sweeps ──
    vec3 haze = mix(colDeep, colPaper, 0.28 + 0.10 * palette);
    float skyT = smoothstep(-0.45, 0.55, p.y);
    vec3 col = mix(haze, colDeep * 0.92, skyT * 0.85);

    float wash = vnoise2(p * vec2(2.2, 4.5) + vec2(hdrift * 0.6, 0.0));
    wash += 0.5 * vnoise2(p * vec2(4.5, 9.0) - vec2(hdrift, 1.3));
    wash /= 1.5;
    col += mix(colDeep, colPaper, 0.55) * (wash - 0.5) * brush * 0.12;

    // Three soft diagonal ink sweeps (wash strokes).
    for (int j = 0; j < 3; ++j) {
        vec2 c = vec2(mix(-0.6, 0.6, hash11(float(j) * 5.1 + hh * 9.0
                          + seed * 3.0)),
                      0.10 + 0.28 * hash11(float(j) * 2.7 + hh * 4.0));
        vec2 q = (p - c) * vec2(0.7, 3.2);
        float s = exp(-dot(q, q) * 7.0);
        col = mix(col, mix(colDeep, colPaper, 0.35), s * brush * 0.10);
    }

    // ── moon disc + enso ring ──
    vec2 mc = vec2(mix(-0.42, 0.42, hash11(hh * 7.0 + 1.0 + seed * 2.0)),
                   0.20 + 0.10 * hash11(hh * 3.0 + 2.0));
    float mr = 0.080 + 0.045 * hash11(hh * 5.0 + 3.0);
    float md = length(p - mc);
    float disc = 1.0 - smoothstep(mr, mr + 0.004, md);
    col = mix(col, mix(colPaper * 1.05, colGold, 0.15), disc * sun * 0.90);
    col += colPaper * exp(-max(md - mr, 0.0) * 9.0) * sun * 0.10;
    float enso = abs(md - mr * 1.28);
    col += colGold * (1.0 - smoothstep(0.0, 0.0035, enso)) *
           sun * 0.35 * (0.4 + 0.6 * wash);

    // ── distant birds (three small ink "v"s) ──
    for (int j = 0; j < 3; ++j) {
        vec2 bc = vec2(mix(-0.5, 0.5, hash11(float(j) * 9.7 + hh * 3.0)),
                       0.16 + 0.22 * hash11(float(j) * 4.3 + hh * 8.0));
        vec2 q = abs(p - bc);
        float wing = q.x * 3.2 + q.y;
        float bird = (1.0 - smoothstep(0.004, 0.008, abs(wing - 0.02)))
                   * (1.0 - smoothstep(0.004, 0.008, q.x));
        bird *= step(q.y, 0.03);
        col = mix(col, colDeep, bird * 0.55 * (0.4 + 0.6 * brush));
    }

    // ── ridgelines, far to near ──
    for (int k = 0; k < 6; ++k) {
        if (float(k) >= nLayers) break;
        float t = float(k) / max(nLayers - 1.0, 1.0);
        float base = mix(0.10, -0.335, t);
        float amp = peaks * mix(0.035, 0.110, t);
        float ph = hh * 11.0 + float(k) * 13.0;

        float ridge = base
            + amp * 2.0 * (vnoise1(p.x * (2.6 + t * 2.0) + ph) - 0.5)
            + amp * 1.0 * (vnoise1(p.x * (6.5 + t * 3.0) + float(k) * 31.0) - 0.5);

        if (float(k) < nPag) {
            float tx = mix(-0.55, 0.55,
                           hash11(float(k) * 3.7 + hh * 11.0 + seed * 5.0));
            float tw = (0.050 + 0.028 * hash11(float(k) * 1.3 + 7.0))
                     * mix(0.80, 1.30, t);
            ridge += pagodaH(p.x - tx, tw);
        }

        // Near layers are darker ink; far layers dissolve into the haze.
        vec3 layerCol = mix(haze, colDeep, 0.30 + 0.62 * t);
        layerCol = mix(layerCol, colAccent,
                       0.05 * t * (1.0 - palette) * 0.6);
        float aa = fwidth(ridge) * 1.2 + 0.0012;
        float cover = 1.0 - smoothstep(-aa, aa, p.y - ridge);
        col = mix(col, layerCol, cover);

        // Wisp of mist hugging this ridge (thicker for far layers).
        float fog = exp(-pow((p.y - ridge) / (0.020 + 0.035 * mistAmt), 2.0));
        col = mix(col, haze, fog * mistAmt * (0.28 - 0.10 * t) * cover);
    }

    // Drifting mist bands across the scene.
    float fogY = -0.06 + 0.025 * sin(iTime * 0.09 + hh * 6.0);
    float band = exp(-pow((p.y - fogY) / 0.075, 2.0));
    band += 0.7 * exp(-pow((p.y - fogY - 0.17) / 0.05, 2.0));
    col = mix(col, haze, band * mistAmt * 0.16);

    // ── lanterns: warm points on the near temples, flaring on kicks ──
    for (int k = 0; k < 3; ++k) {
        float kk = nLayers - 1.0 - float(k);   // near layers only
        if (kk < 0.0 || kk >= nPag) continue;
        float t = kk / max(nLayers - 1.0, 1.0);
        float base = mix(0.10, -0.335, t);
        float tx = mix(-0.55, 0.55,
                       hash11(kk * 3.7 + hh * 11.0 + seed * 5.0));
        float tw = (0.050 + 0.028 * hash11(kk * 1.3 + 7.0)) * mix(0.8, 1.3, t);
        for (int j = 0; j < 3; ++j) {
            float lane = hash11(float(j) * 4.9 + kk * 2.3 + hh * 7.0);
            float lx = tx + (lane - 0.5) * tw * 2.4;
            float ly = base + pagodaH(lx - tx, tw)
                     * (0.28 + 0.50 * hash11(float(j) * 8.1 + kk * 5.7));
            float d = length(p - vec2(lx, ly));
            float glow = exp(-d * 170.0) * lanterns * (0.35 + 0.65 * t);
            float halo = exp(-d * 42.0) * lanterns * (0.35 + 0.65 * t);
            col += (colGold + colAccent * 0.6) *
                   (glow * (0.55 + 0.75 * kick) + halo * 0.22);
        }
    }

    // ── paper vignette ──
    vec2 qv = p * vec2(0.9, 1.25);
    col *= 1.0 - smoothstep(0.62, 1.05, length(qv)) * 0.55;
    col += (hash21(fragCoord) - 0.5) * 0.015;   // paper grain

    fragColor = vec4(col, 1.0);
}
