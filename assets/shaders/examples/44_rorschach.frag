// 44 · Rorschach — a mirrored ink blot on parchment, grown from value
// noise and domain warp. The seed image tints the paper and the ink
// (palette 0) or the classic black-on-cream (palette 1). Kicks bloom the
// blot outward; a vermilion seal dot marks the fold. Deterministic per
// seed + knobs. MIT License.

uniform float ink;     // @range 0..1  default=0.50  blot coverage
uniform float scale;   // @range 0.5..4 default=1.25 blot structure size
uniform float warp;    // @range 0..1  default=0.45  organic distortion
uniform float soft;    // @range 0..1  default=0.25  ink edge softness
uniform float palette; // @range 0..1  default=0.30  0=paper-tinted 1=classic
uniform float kick;    // @range 0..1  default=0.40
uniform float seed;    // @range 0..1  default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x),
               f.y);
}

float fbm(vec2 p) {
    float v = 0.0, amp = 0.55;
    for (int i = 0; i < 4; ++i) {
        v += amp * vnoise(p);
        p = p * 2.03 + vec2(17.3, 9.1);
        amp *= 0.55;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;

    // ── seed probes: paper + ink tint ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3) * 0.25;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hh = fract(dot(avg + s2 * 0.31,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    vec3 paper = mix(clamp(avg * 0.85 + lum * 0.5 + 0.22, 0.0, 1.0),
                     vec3(0.905, 0.870, 0.790), 0.4 + 0.6 * palette);
    vec3 inkCol = mix(avg * 0.15 + vec3(0.012, 0.010, 0.014),
                      vec3(0.028, 0.026, 0.030), 0.4 + 0.6 * palette);
    vec3 stain = mix(clamp(avg * 1.2 + 0.2, 0.0, 1.0),
                     vec3(0.80, 0.76, 0.66), 0.5 + 0.5 * palette);
    vec3 seal = mix(clamp(s2 * 1.4 + 0.08, 0.0, 1.0),
                    vec3(0.560, 0.130, 0.090), 0.4 + 0.6 * palette);

    float k = clamp(iKick, 0.0, 1.0) * kick;
    float t = iTime * 0.02;

    // ── mirrored blot coordinates (x mirrored about the fold) ──
    vec2 q = vec2(abs(p.x), p.y) * scale;
    // domain warp
    vec2 w1 = vec2(vnoise(q * 0.7 + hh * 13.0 + t),
                   vnoise(q * 0.7 + 5.2 - t));
    q += (w1 - 0.5) * 2.0 * warp * 0.8;
    float v = fbm(q + hh * 7.0);
    // A central mass so the blot reads as one organism, not edge stains.
    v += 0.17 * smoothstep(0.78, 0.05, length(p));

    // ── paper: warm cream, grain, a faint fold line ──
    vec3 col = paper * (0.92 + 0.08 * smoothstep(1.0, 0.0, length(p)));
    col += (hash21(fragCoord + seed * 5.0) - 0.5) * 0.03;
    float fold = exp(-abs(p.x) * 90.0) * 0.05;
    col *= 1.0 - fold;

    // ── ink blot ──
    float thr = mix(0.72, 0.38, ink) - 0.16 * k;
    float edge = 0.015 + 0.09 * soft;
    float blot = smoothstep(thr - edge, thr + edge, v);

    // Ink is darker where the noise is dense; a wet halo at the edge.
    vec3 body = inkCol * (0.75 + 0.5 * smoothstep(thr, thr + 0.3, v));
    col = mix(col, body, blot);
    float halo = smoothstep(thr - edge * 4.0, thr, v) * (1.0 - blot);
    col = mix(col, stain, halo * (0.25 + 0.35 * k));

    // ── vermilion seal at the fold, low on the sheet ──
    vec2 sp = vec2(0.0, -0.40) + vec2(sin(hh * TAU) * 0.02, 0.0);
    float sd = length(p - sp);
    float sealCov = 1.0 - smoothstep(0.020, 0.023, max(abs(p.x - sp.x),
                                                       abs(p.y - sp.y)));
    // carved cross knockout (a tiny mark, like a chop)
    float carve = (1.0 - smoothstep(0.002, 0.004, abs(p.x - sp.x)))
                * step(abs(p.y - sp.y), 0.014)
                + (1.0 - smoothstep(0.002, 0.004, abs(p.y - sp.y)))
                * step(abs(p.x - sp.x), 0.014);
    sealCov *= 1.0 - clamp(carve, 0.0, 1.0);
    col = mix(col, seal, sealCov * 0.9);

    // Sheet vignette.
    col *= 1.0 - smoothstep(0.6, 1.15, length(p * vec2(0.8, 1.15))) * 0.25;

    fragColor = vec4(col, 1.0);
}
