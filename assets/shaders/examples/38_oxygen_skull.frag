// 38 · Oxygen Skull — the lower half of the Oxygène homage (Michel
// Granger's earth-as-egg, skull inside). A rotating human skull (a glTF
// model clip renders into iChannel2) drifts in a deep oxblood void:
// warm bone, ember glow inside the cavities, a cold atmospheric rim
// from the planet above, dust motes that pulse on kicks. Opaque — it is
// the background the globe crossfades over (39_oxygen_globe.frag).
//
// Model transform uniforms are read by the engine's M3DRenderer, so
// A–H knobs, LFOs and clip automation drive the pose for free.
// Deterministic per time + knobs. MIT License.

uniform float modelPosX;      // @range -1..1   default=0.0
uniform float modelPosY;      // @range -1..1   default=0.02
uniform float modelPosZ;      // @range -1..1   default=0.0
uniform float modelRotX;      // @range -90..90 default=-3
uniform float modelRotY;      // @range -180..180 default=0
uniform float modelRotZ;      // @range -90..90 default=0
uniform float modelSpinY;     // @range -30..30 default=6   degrees/second
uniform float modelScale;     // @range 0.2..3  default=0.72
uniform float lightYaw;       // @range -180..180 default=-30
uniform float lightPitch;     // @range -90..90 default=18
uniform float lightAmbient;   // @range 0..1    default=0.30
uniform float lightIntensity; // @range 0..3    default=0.90

uniform float ember;   // @range 0..1 default=0.85 cavity fire
uniform float sky;     // @range 0..1 default=0.70 void wash
uniform float palette; // @range 0..1 default=0.40 warm oxblood → deep space
uniform float kick;    // @range 0..1 default=0.45 audio response
uniform float seed;    // @range 0..1 default=0.0  alternate take

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    vec2 uv = fragCoord / res;
    float r = length(p);
    float t = iTime;
    float k = clamp(iKick, 0.0, 1.0) * kick;
    float hh = fract(sin(seed * 91.7 + 4.1) * 43758.5453);

    // ── the void: oxblood core, colder edges, a breathing halo ──
    vec3 colOx = mix(vec3(0.085, 0.016, 0.018),
                     vec3(0.035, 0.050, 0.105), palette);
    vec3 col = colOx * (0.55 + 0.45 * smoothstep(0.9, 0.0, r));
    float halo = exp(-r * 2.2) * (0.35 + 0.65 * sky) * (0.8 + 0.5 * k);
    col += mix(vec3(0.75, 0.22, 0.10), vec3(0.30, 0.48, 0.85), palette)
         * halo * 0.16;
    // slow void breath
    col *= 0.92 + 0.08 * sin(t * 0.23 + hh * TAU);

    // ── drifting dust motes ──
    for (int i = 0; i < 14; ++i) {
        float hi = hash11(float(i) * 3.17 + hh * 9.0);
        float hj = hash11(float(i) * 7.71 + hh * 3.0);
        vec2 c = vec2(mix(-0.75, 0.75, hi),
                      mix(-0.45, 0.45, hj)
                      + 0.02 * sin(t * (0.2 + 0.3 * hj) + hi * TAU));
        float d = length(p - c);
        float mote = exp(-d * 160.0) * (0.25 + 0.75 * hi);
        col += mix(vec3(0.9, 0.45, 0.2), vec3(0.4, 0.6, 1.0), hj)
             * mote * (0.35 + 0.65 * k) * 0.55;
    }

    // ── the skull (model render on iChannel2) ──
    vec4 m = texture(iChannel2, uv);
    float alpha = m.a;
    float lum = dot(m.rgb, vec3(0.299, 0.587, 0.114));

    // Bone: warm cream with cool shadowed hollows.
    vec3 bone = mix(vec3(0.20, 0.15, 0.12), vec3(0.96, 0.88, 0.72),
                    smoothstep(0.02, 0.85, lum));
    bone = mix(bone, vec3(0.90, 0.86, 0.78), 0.25 * palette);

    // Cavities smoulder: the darker the render, the hotter the ember.
    float cavity = (1.0 - smoothstep(0.03, 0.32, lum)) * alpha;
    float flicker = 0.75 + 0.25 * sin(t * 3.1 + uv.y * 11.0 + hh * TAU);
    vec3 emberCol = mix(vec3(1.0, 0.32, 0.08), vec3(1.0, 0.55, 0.16),
                        0.5 + 0.5 * sin(uv.x * 7.0));
    vec3 skullCol = bone * (0.55 + 0.6 * lum);
    skullCol += emberCol * cavity * ember * flicker * (0.5 + 1.4 * k);

    // Cold atmospheric rim from the planet side (silhouette via alpha
    // gradients), warm underlight from below.
    float rim = clamp((fwidth(alpha)) * 140.0, 0.0, 1.0) * alpha;
    vec3 rimCol = mix(vec3(1.0, 0.35, 0.10), vec3(0.35, 0.55, 0.95),
                      smoothstep(-0.2, 0.4, p.y) * (0.4 + 0.6 * palette));
    skullCol += rimCol * rim * (0.5 + 0.6 * k);

    col = mix(col, skullCol, alpha);

    // A soft ground shadow / glow under the skull to seat it in the void.
    col *= 1.0 - 0.35 * smoothstep(0.55, 0.0, r) * (1.0 - alpha) * 0.25;

    // Paper grain + vignette.
    col += (hash21(fragCoord + seed * 13.0) - 0.5) * 0.018;
    col *= 1.0 - smoothstep(0.55, 1.15, r) * 0.55;

    fragColor = vec4(col, 1.0);
}
