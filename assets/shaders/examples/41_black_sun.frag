// 41 · Black Sun — a total eclipse as an occult emblem: a black disc
// with a blazing corona, a diamond-ring glint, crepuscular rays and a
// sparse star field behind. Kick pumps the corona and drifts the glint.
// Palette 0 = picture-tinted (seed image on iChannel2), 1 = ember/ice.
// MIT License.

uniform float disc;    // @range 0.05..0.35 default=0.16 moon radius
uniform float corona;  // @range 0..1  default=0.85
uniform float rays;    // @range 0..1  default=0.55
uniform float glint;   // @range 0..1  default=0.75 diamond ring
uniform float palette; // @range 0..1  default=0.45
uniform float kick;    // @range 0..1  default=0.55
uniform float seed;    // @range 0..1  default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);
    float th = atan(p.y, p.x);

    // ── seed picture probes: palette (family convention) ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 avg = (s0 + s2 + s3) / 3.0;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hh = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    vec3 colSpace = mix(avg * 0.10 + vec3(0.006, 0.008, 0.018),
                        vec3(0.012, 0.016, 0.040), palette);
    vec3 colFire  = mix(clamp(avg * vec3(1.6, 1.1, 0.5) + 0.25, 0.0, 1.0),
                        vec3(1.00, 0.62, 0.24), palette);
    vec3 colIce   = mix(clamp(avg * vec3(0.7, 1.0, 1.5) + 0.15, 0.0, 1.0),
                        vec3(0.62, 0.82, 1.00), 0.4 + 0.6 * palette);

    float k = clamp(iKick, 0.0, 1.0) * kick;
    float d0 = disc * (1.0 + 0.04 * k);

    // ── space + sparse stars ──
    vec3 col = colSpace * (0.7 + 0.3 * smoothstep(0.9, 0.0, r));
    for (int i = 0; i < 2; ++i) {
        float sc = i == 0 ? 9.0 : 17.0;
        vec2 cell = floor(p * sc);
        vec2 jit = vec2(hash21(cell + float(i) * 7.1),
                        hash21(cell + float(i) * 3.7 + 5.0));
        vec2 sp = (cell + jit) / sc;
        float d = length(p - sp);
        float tw = 0.6 + 0.4 * sin(iTime * (1.0 + jit.x) + jit.y * TAU);
        col += colIce * exp(-d * 260.0 / (0.5 + 0.5 * float(i)))
             * tw * (0.35 - 0.12 * float(i));
    }

    // ── corona: soft halo + sharp rim + rays ──
    float outside = max(r - d0, 0.0);
    float halo = exp(-outside * 14.0) * 0.55 + exp(-outside * 60.0) * 0.75;
    halo *= corona * (0.55 + 0.75 * k);
    col += colFire * halo * 0.55;

    float rim = exp(-abs(r - d0 * 1.015) * 900.0);
    col += mix(colFire, colIce, 0.35) * rim * corona * (0.8 + 0.6 * k) * 1.4;

    // Crepuscular rays, slow rotation.
    float rayPhase = th * 9.0 + iTime * 0.06 + hh * TAU;
    float ra = pow(0.5 + 0.5 * sin(rayPhase), 10.0);
    ra += 0.6 * pow(0.5 + 0.5 * sin(rayPhase * 2.7 + 1.3), 14.0);
    float rayFall = exp(-outside * 6.0) * smoothstep(0.0, 0.02, outside);
    col += colFire * ra * rayFall * rays * (0.35 + 0.65 * k) * 0.8;

    // ── diamond ring glint ──
    float glintAng = hh * TAU + iTime * 0.10 + k * 0.6;
    vec2 gc = vec2(cos(glintAng), sin(glintAng)) * d0;
    float gd = length(p - gc);
    col += mix(colIce, vec3(1.0), 0.5) * exp(-gd * 170.0)
         * glint * (0.6 + 0.8 * k) * 1.5;

    // ── the black disc (with a breath of rim light) ──
    float inDisc = 1.0 - smoothstep(d0, d0 + 0.0025, r);
    col = mix(col, vec3(0.004, 0.003, 0.006), inDisc);
    col += colFire * exp(-max(d0 - r, 0.0) * 90.0) * inDisc * 0.15;

    col += (hash21(fragCoord + seed * 11.0) - 0.5) * 0.014;
    col *= 1.0 - smoothstep(0.55, 1.2, r) * 0.5;

    fragColor = vec4(col, 1.0);
}
