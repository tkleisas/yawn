// 47 · Gargantua — an Interstellar-style black hole: a razor photon ring
// around a pitch-black shadow, a hot accretion disk in perspective with
// Doppler beaming (one side brighter), the disk's far side lensed up
// over the shadow as a glowing arc, and a star field warped by the
// gravitational lens (stars smear into a ring near the hole). Kicks
// brighten the disk and ring; `spin` drives the orbital bands.
// Deterministic per time + knobs. MIT License.

uniform float disk;    // @range 0..1  default=0.90 accretion brightness
uniform float incl;    // @range 0.06..0.5 default=0.24 disk thinness
uniform float tilt;    // @range -30..30 default=-8 disk tilt (degrees)
uniform float spin;    // @range -3..3  default=0.9  orbital speed
uniform float photon;  // @range 0..1  default=0.95 photon ring
uniform float lens;    // @range 0..1  default=0.85 lensing strength
uniform float stars;   // @range 0..1  default=0.70 star field
uniform float kick;    // @range 0..1  default=0.55
uniform float palette; // @range 0..1  default=0.50 warm ↔ cool
uniform float seed;    // @range 0..1  default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Two-layer star field sampled at lens-warped coordinates.
vec3 starLayer(vec2 p, float sc, float t, float layer) {
    vec2 g = p * sc + layer * 17.0;
    vec2 cell = floor(g);
    vec2 jit = vec2(hash21(cell + layer * 3.1),
                    hash21(cell + layer * 7.7 + 5.0));
    float present = step(0.90, hash21(cell + layer * 11.0 + 1.0));
    float d = length(g - (cell + jit));
    float tw = 0.6 + 0.4 * sin(t * (0.6 + 1.4 * jit.y) + jit.x * TAU);
    return vec3(0.75, 0.85, 1.0)
         * exp(-d * (14.0 / (0.75 + 0.5 * jit.x))) * present * tw;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);

    float hh = fract(sin(seed * 61.7 + 3.3) * 43758.5453);
    float k = clamp(iKick, 0.0, 1.0) * kick;
    float t = iTime;

    const float rs = 0.105;                 // shadow radius
    vec3 diskHot = mix(vec3(1.00, 0.62, 0.24), vec3(1.0, 0.86, 0.62),
                      0.35 + 0.5 * palette);
    vec3 diskCool = mix(vec3(0.85, 0.42, 0.14), vec3(0.70, 0.82, 1.00),
                        0.3 + 0.5 * palette);

    // ── lens-warped star field ──
    // Deflection ~ rs²/r: coordinates push outward near the hole, so
    // stars crowd into a bright Einstein ring just outside the shadow.
    vec2 lq = p * (1.0 + lens * rs * rs / max(r * r, 0.0015));
    vec3 stars3 = starLayer(lq, 18.0, t, 0.0)
                + starLayer(lq, 34.0, t, 1.0) * 0.6;
    vec3 col = vec3(0.008, 0.010, 0.020) + stars3 * stars * 0.55;

    // faint nebula wash
    col += vec3(0.02, 0.025, 0.05)
         * (0.5 + 0.5 * sin(p.x * 3.0 + t * 0.02 + hh * 6.0))
         * 0.35;

    // ── accretion disk (annulus in tilted elliptical coordinates) ──
    float ca = cos(radians(tilt)), sa = sin(radians(tilt));
    vec2 d = mat2(ca, -sa, sa, ca) * p;
    vec2 e = vec2(d.x, d.y / max(incl, 0.02));
    float eR = length(e) / 0.62;            // 0 at centre, 1 at outer rim
    float ang = atan(e.y, e.x);

    // Orbital bands: differential rotation (inner bands faster).
    float phase = ang * 7.0 + log(max(eR, 0.05)) * 24.0 - t * spin * 2.2;
    float bands = 0.55 + 0.45 * sin(phase);
    float fine  = 0.75 + 0.25 * sin(phase * 3.1 + 1.7);
    // Doppler beaming: the approaching side (left) is brighter.
    float beam = 0.45 + 0.95 * smoothstep(-1.0, 1.0, -e.x);

    float diskMask = smoothstep(1.0, 0.94, eR) * smoothstep(0.62, 0.72, eR);
    float diskBright = diskMask * bands * fine * beam
                     * disk * (0.6 + 0.7 * k);
    vec3 diskCol = mix(diskCool, diskHot, 0.35 + 0.65 * bands);

    // ── lensed far side: the disk's back arcs over/under the shadow ──
    float arcBand = exp(-pow((r - rs * 1.42) / (rs * 0.30), 2.0));
    // Brighter where the disk would be seen edge-on (left/right).
    float arcMask = arcBand * (0.35 + 0.65 * pow(abs(cos(atan(p.y, p.x))),
                                                 1.5));
    arcMask *= lens * (0.55 + 0.45 * bands);

    // ── compose: stars+disk (behind), shadow, then the near disk ──
    col += diskCol * diskBright;
    col += diskCol * arcMask * disk * (0.5 + 0.6 * k) * 0.55;

    // photon ring (razor thin, slightly warm white)
    float ringR = rs * 1.015;
    float ring = exp(-abs(r - ringR) * 1500.0) * photon;
    col += mix(vec3(1.0, 0.92, 0.78), diskHot, 0.4) * ring
         * (1.1 + 0.7 * k);
    col += diskHot * exp(-abs(r - ringR) * 90.0) * photon * 0.25
         * (0.7 + 0.6 * k);

    // shadow last (occludes far-side disk + stars)
    float shadow = 1.0 - smoothstep(rs, rs + 0.0018, r);
    col *= 1.0 - shadow;

    // near-side disk passes IN FRONT of the shadow: only the disk band
    // itself (diskMask), lower half of the ellipse.
    float front = diskMask * smoothstep(0.0, 0.02, d.y + 0.01);
    col += diskCol * diskBright * front * 1.1;

    // hot inner edge just OUTSIDE the ring (gated, or it fills the shadow)
    float innerEdge = exp(-max(r - rs, 0.0) * 70.0)
                    * smoothstep(rs, rs + 0.004, r);
    col += diskHot * innerEdge * disk * 0.30 * (0.6 + 0.7 * k);

    col += (hash21(fragCoord + seed * 5.0) - 0.5) * 0.012;
    col *= 1.0 - smoothstep(0.55, 1.2, r) * 0.45;

    fragColor = vec4(col, 1.0);
}
