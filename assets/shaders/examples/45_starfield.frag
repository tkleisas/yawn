// 45 · Starfield — deep space parallax: three star layers at different
// cell scales and drift speeds over a slow nebula wash. Palette 0 =
// picture-tinted (seed image on iChannel2), 1 = cool lapis space.
// Opaque background; deterministic per time + knobs. MIT License.

uniform float density; // @range 0..1  default=0.60 star density
uniform float drift;   // @range -2..2 default=0.15 parallax drift
uniform float nebula;  // @range 0..1  default=0.50 nebula strength
uniform float twinkle; // @range 0..1  default=0.55
uniform float palette; // @range 0..1  default=0.35
uniform float seed;    // @range 0..1  default=0.0

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
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += amp * vnoise(p);
        p = p * 2.1 + vec2(11.7, 5.3);
        amp *= 0.55;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);

    // ── seed probes: palette (family convention) ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 avg = (s0 + s2 + s3) / 3.0;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hh = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    vec3 deepA = mix(avg * 0.08 + vec3(0.008, 0.010, 0.022),
                     vec3(0.010, 0.014, 0.038), palette);
    vec3 deepB = mix(avg * 0.14 + vec3(0.012, 0.010, 0.020),
                     vec3(0.030, 0.018, 0.055), palette);
    vec3 starCol = mix(clamp(avg * 1.4 + 0.25, 0.0, 1.0),
                       vec3(0.80, 0.88, 1.00), 0.4 + 0.6 * palette);
    vec3 warmCol = mix(clamp(avg * vec3(1.5, 1.1, 0.6) + 0.2, 0.0, 1.0),
                       vec3(0.95, 0.72, 0.42), palette);

    vec3 col = mix(deepA, deepB, smoothstep(-0.5, 0.6, p.y + 0.1 * sin(p.x * 1.3)));

    // ── nebula wash (two tinted fbm fields) ──
    vec2 np = p * 1.9 + vec2(hh * 11.0 - iTime * drift * 0.02, seed * 5.0);
    float n1 = fbm(np);
    float n2 = fbm(np * 1.7 + vec2(4.1, 2.3) - iTime * 0.01);
    col += mix(deepB, warmCol, 0.35) * smoothstep(0.35, 0.95, n1)
         * nebula * 0.50;
    col += mix(deepA, starCol, 0.30) * smoothstep(0.45, 1.0, n2)
         * nebula * 0.32;

    // ── three star layers ──
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float sc = 16.0 + fi * 17.0;
        float parallax = drift * (0.02 + 0.05 * fi);
        vec2 sp = p + vec2(iTime * parallax, 0.0);
        vec2 g = sp * sc + hh * 7.0 + fi * 19.0;
        vec2 cell = floor(g);
        vec2 jit = vec2(hash21(cell + fi * 3.1),
                        hash21(cell + fi * 7.7 + 5.0));
        // Density threshold from the layer + a per-cell roll.
        float present = step(1.0 - density * (0.24 - 0.05 * fi),
                             hash21(cell + fi * 11.0 + 1.0));
        vec2 starPos = cell + jit;
        float d = length(g - starPos);
        float bright = exp(-d * (14.0 / (0.75 + 0.5 * jit.x)));
        float tw = 0.55 + 0.45 * sin(iTime * (0.7 + 2.0 * jit.y)
                                     + jit.x * TAU);
        tw = mix(1.0, tw, twinkle);
        col += starCol * bright * present * tw * (0.75 - 0.16 * fi);
    }

    // ── faint milky band across the sky ──
    float band = exp(-pow((p.y + 0.16 * p.x - 0.05) / 0.16, 2.0));
    col += mix(deepB, starCol, 0.35) * band * 0.10 * nebula;

    col += (hash21(fragCoord + seed * 3.0) - 0.5) * 0.012;
    col *= 1.0 - smoothstep(0.6, 1.25, r) * 0.35;

    fragColor = vec4(col, 1.0);
}
