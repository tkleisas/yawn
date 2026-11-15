// 39 · Oxygen Globe — the upper half of the Oxygène homage. A rotating
// low-poly Earth (glTF model render on iChannel2) with a slow egg-shaped
// stretch, a cold atmosphere halo, and a seeded fissure network: the
// fractures glow hottest during the morph (they are the energy of the
// transition) and the cracked-open pixels drop their alpha, revealing
// the skull layer underneath. `morph` crossfades the whole planet from
// 0 = 100% skull to 1 = 100% Earth — one playable knob for the mix.
//
// Model transform uniforms are read by the engine's M3DRenderer.
// Output is premultiplied alpha. Deterministic per time + knobs.
// MIT License.

uniform float modelPosY;      // @range -1..1   default=0.0
uniform float modelRotX;      // @range -90..90 default=-12
uniform float modelSpinY;     // @range -30..30 default=4   degrees/second
uniform float modelScale;     // @range 0.2..3  default=0.95
uniform float lightYaw;       // @range -180..180 default=-40
uniform float lightPitch;     // @range -90..90 default=25
uniform float lightAmbient;   // @range 0..1    default=0.40
uniform float lightIntensity; // @range 0..3    default=1.00

uniform float morph;   // @range 0..1  default=1.0  0 = skull, 1 = Earth
uniform float autoMix; // @range 0..1  default=0.0  slow automatic morph cycle
uniform float crack;   // @range 0..1  default=0.50 fissure network amount
uniform float crackPos;// @range -0.4..0.4 default=0.02 fissure height
uniform float egg;     // @range 0..1  default=0.35 egg-shaped stretch
uniform float ember;   // @range 0..1  default=0.80 fissure glow
uniform float atmo;    // @range 0..1  default=0.75 atmosphere halo
uniform float palette; // @range 0..1  default=0.35 cool grade strength
uniform float kick;    // @range 0..1  default=0.40 audio response
uniform float seed;    // @range 0..1  default=0.0  alternate fracture

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    vec2 uv = fragCoord / res;
    float t = iTime;
    float k = clamp(iKick, 0.0, 1.0) * kick;
    float hh = fract(sin(seed * 71.3 + 2.7) * 43758.5453);
    // The mix: the morph knob, optionally breathing on its own.
    float autoMorph = 0.5 - 0.5 * cos(t * 0.22 + hh * TAU);
    float m = mix(morph, autoMorph, autoMix);

    // ── egg stretch: sample the model slightly taller than round ──
    vec2 euv = 0.5 + (uv - 0.5) * vec2(1.0, 1.0 - 0.16 * egg);
    vec4 globe = texture(iChannel2, euv);
    float cover = globe.a;
    vec3 col = globe.rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));

    // Cinematic grade: cool the oceans, keep the land warm, lift contrast.
    vec3 graded = mix(col, col * vec3(0.86, 0.96, 1.10), 0.35 + 0.45 * palette);
    graded = mix(graded, vec3(lum) * vec3(0.55, 0.72, 1.0),
                 0.12 * palette);
    graded += graded * (0.18 + 0.22 * k) * smoothstep(0.15, 0.9, lum);
    col = graded;

    // ── fissure network: a main jagged line + seeded branches ──
    // Jaw the line with three noise octaves so it reads as a fracture,
    // not a wave.
    float lineY = crackPos
                + 0.050 * sin(p.x * 4.0 + hh * TAU)
                + 0.022 * sin(p.x * 13.0 + hh * 6.0)
                + 0.010 * sin(p.x * 41.0 + hh * 11.0);
    float dMain = abs(p.y - lineY);
    float widthMod = 0.65 + 0.35 * sin(p.x * 33.0 + hh * 5.0)
                          * sin(p.x * 9.0 + hh);
    float w = (0.004 + 0.065 * crack) * widthMod;
    float fissure = 1.0 - smoothstep(w, w + 0.003, dMain);
    float hot = exp(-max(dMain - w, 0.0) * 130.0);

    for (int i = 0; i < 4; ++i) {
        float hi = hash11(float(i) * 3.31 + hh * 5.0);
        vec2 a = vec2(mix(-0.32, 0.32, hi),
                      lineY + 0.015 * sin(hi * 9.0));
        vec2 b = a + vec2((hash11(float(i) * 7.7 + hh) - 0.5) * 0.38,
                          (i % 2 == 0 ? 1.0 : -1.0) *
                          (0.10 + 0.16 * hash11(float(i) * 1.9 + hh * 3.0)));
        float bd = segDist(p, a, b);
        float bw = (0.002 + 0.024 * crack) * (0.65 + 0.35 * hi);
        fissure = max(fissure, 1.0 - smoothstep(bw, bw + 0.0025, bd));
        hot = max(hot, exp(-max(bd - bw, 0.0) * 150.0));
    }
    hot *= cover;
    fissure *= cover;   // only inside the planet

    // The transition burns: fissures blaze at half morph.
    float energy = sin(clamp(m, 0.0, 1.0) * PI);
    float fissureGlow = fissure * ember * (0.25 + 0.75 * energy)
                      * (0.55 + 0.9 * k);
    vec3 emberCol = mix(vec3(1.0, 0.30, 0.07), vec3(1.0, 0.62, 0.20),
                        0.5 + 0.5 * sin(p.x * 9.0 + hh * 4.0));

    // ── atmosphere: halo just outside the silhouette ──
    float r = length(p - vec2(0.0, modelPosY));
    float rGlobe = 0.44 * modelScale * (1.0 - 0.06 * egg);
    float halo = exp(-max(r - rGlobe, 0.0) * 70.0)
               * smoothstep(rGlobe * 0.55, rGlobe * 0.95, r);
    halo *= atmo * m * (0.45 + 0.55 * k);
    vec3 atmoCol = mix(vec3(0.25, 0.45, 0.85), vec3(0.35, 0.70, 1.0),
                       0.5 + 0.5 * sin(t * 0.4 + hh * TAU));

    // Cracked pixels open up: the skull shows through the shell.
    float hole = fissure * (0.80 + 0.20 * hash11(hh * 13.0));
    float bodyA = cover * m * (1.0 - hole);

    // Image-space premultiplied composite:
    //   planet body (alpha = morph) + fissure ember + atmosphere glow.
    vec3 premul = col * bodyA
                + emberCol * fissureGlow * 0.85 * m
                + emberCol * hot * 0.16 * ember * m * (0.6 + 0.6 * k)
                + atmoCol * halo * 0.50;
    float a = clamp(bodyA + halo * (1.0 - cover) * 0.55, 0.0, 1.0);

    // Slight warm interior tint bleeding at the fissure edges, as if the
    // shell is molten just under the surface.
    premul += vec3(0.9, 0.35, 0.12) * fissure * 0.10 * m;

    fragColor = vec4(premul, a);
}
