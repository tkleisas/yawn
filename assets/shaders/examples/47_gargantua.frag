// 47 · Gargantua — a ray-traced Schwarzschild black hole in the spirit of
// Interstellar's Gargantua. Each pixel is a photon integrated through a
// weak-field curved path (a = -1.5 h² r / r⁵, which reproduces the
// photon-ring and the double image of the disk): the accretion disk is
// sampled every time the ray crosses its equatorial plane, so the far
// side of the disk is lensed up over the shadow as the iconic bright
// arcs while the near side crosses in front. Doppler beaming brightens
// the approaching side, the photon ring glows at the critical impact
// parameter, and the escaped rays sample a star field warped by the
// same lensing. Kicks brighten the disk.
// MIT License.

uniform float disk;    // @range 0..1  default=0.90 accretion brightness
uniform float incl;    // @range 0.0..0.5 default=0.10 camera elevation
uniform float tilt;    // @range -30..30 default=-6 disk tilt (degrees)
uniform float spin;    // @range -3..3  default=1.0  orbital speed
uniform float photon;  // @range 0..1  default=0.85 photon-ring glow
uniform float lens;    // @range 0.5..1.5 default=1.0 lensing strength
uniform float stars;   // @range 0..1  default=0.55 star field
uniform float kick;    // @range 0..1  default=0.45
uniform float palette; // @range 0..1  default=0.50 white-hot ↔ amber
uniform float seed;    // @range 0..1  default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

// World units: the shadow's angular radius is rs / camera distance.
const float kBlackHoleR = 0.40;     // Schwarzschild radius
const float kCamDist   = 4.6;
const float kDiskInner = 1.05;      // ISCO-ish
const float kDiskOuter = 2.60;

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

vec3 starField(vec2 dir, float t) {
    vec3 c = vec3(0.0);
    for (int i = 0; i < 2; ++i) {
        float sc = 16.0 + float(i) * 17.0;
        vec2 g = dir * sc + float(i) * 19.0;
        vec2 cell = floor(g);
        vec2 jit = vec2(hash21(cell + float(i) * 3.1),
                        hash21(cell + float(i) * 7.7 + 5.0));
        float present = step(0.90, hash21(cell + float(i) * 11.0 + 1.0));
        float d = length(g - (cell + jit));
        float tw = 0.6 + 0.4 * sin(t * (0.6 + 1.4 * jit.y) + jit.x * TAU);
        c += vec3(0.72, 0.84, 1.0)
           * exp(-d * (14.0 / (0.75 + 0.5 * jit.x))) * present * tw
           * (0.55 - 0.15 * float(i));
    }
    return c;
}

// Disk emission at a crossing point (world space, plane y = 0).
vec3 diskEmission(vec3 hit, vec3 rayDir, float t, float k) {
    float rr = length(hit.xz);
    if (rr < kDiskInner || rr > kDiskOuter) return vec3(0.0);

    float ang = atan(hit.z, hit.x);
    // Fine concentric striations (lensed many radii overlap) + a slower
    // orbital ripple.
    float fine = 0.72 + 0.28 * sin(rr * 23.0 - t * spin * 2.2 + ang * 3.0);
    float slow = 0.75 + 0.25 * sin(ang * 5.0 + rr * 9.0 - t * spin);
    float tex = mix(0.6, 1.0, vnoise(vec2(rr * 8.0, ang * 2.0 + t * 0.1)));

    // Temperature: white-hot inner edge, amber outward.
    float heat = smoothstep(kDiskOuter, kDiskInner, rr);
    vec3 hot  = mix(vec3(1.00, 0.93, 0.82), vec3(1.0, 0.99, 0.97), 0.5);
    vec3 warm = mix(vec3(1.00, 0.42, 0.10), vec3(1.00, 0.72, 0.30), palette);
    vec3 col = mix(warm, hot, pow(heat, 1.4));

    // Doppler beaming: the side rotating toward the camera is brighter.
    vec3 vdir = normalize(vec3(hit.z, 0.0, -hit.x));   // orbital direction
    float mu = dot(normalize(rayDir), vdir);           // toward the camera
    float beam = clamp(1.0 + 0.85 * mu, 0.18, 2.4);

    // Brightness profile: a hot rim at the inner edge, falling off out.
    float profile = smoothstep(kDiskInner, kDiskInner * 1.25, rr)
                  * (1.0 - smoothstep(kDiskOuter * 0.75, kDiskOuter, rr));
    profile *= 0.55 + 0.45 * heat;

    float bright = disk * (0.55 + 0.75 * k) * fine * slow * tex
                 * profile * beam * 1.7;
    return col * bright;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float hh = fract(sin(seed * 61.7 + 3.3) * 43758.5453);
    float k = clamp(iKick, 0.0, 1.0) * kick;

    // Screen tilt for composition (disk plane tilt is handled by the camera;
    // this rotates the whole image so the approach side sits left or right).
    float ca = cos(radians(tilt)), sa = sin(radians(tilt));
    vec2 sp = mat2(ca, -sa, sa, ca) * p;

    // Camera slightly above the disk plane, looking AT the hole.
    vec3 camPos = vec3(0.0, incl * 1.2, kCamDist);
    vec3 fwd = normalize(-camPos);
    vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, fwd);
    const float kFocal = 1.45;
    vec3 rd = normalize(fwd * kFocal + right * sp.x + up * sp.y);

    // Impact parameter (conserved): b = |camPos × rd| — the photon ring
    // sits at b = 3√3/2 · rs.
    float bImpact = length(cross(camPos, rd));
    float bCrit = 1.5 * sqrt(3.0) * kBlackHoleR;

    // Photon integration (weak-field Schwarzschild approximation):
    //   a = -1.5 · rs · h² · pos / r⁵,  h = pos × vel (conserved).
    vec3 pos = camPos;
    vec3 vel = rd;
    vec3 hvec = cross(pos, vel);
    float h2 = dot(hvec, hvec) * lens;

    vec3 col = vec3(0.0);
    float captured = 0.0;
    float minR = 1e9;
    bool escaped = false;
    vec3 escapeDir = rd;
    float prevY = pos.y;

    const int STEPS = 110;
    for (int i = 0; i < STEPS; ++i) {
        float r = length(pos);
        minR = min(minR, r);
        if (r < kBlackHoleR) { captured = 1.0; break; }
        if (r > 7.0) { escaped = true; escapeDir = normalize(vel); break; }

        float dt = 0.055 * clamp(r, 0.35, 3.0);
        vec3 prevPos = pos;
        vec3 acc = -1.5 * kBlackHoleR * h2 * pos / pow(r, 5.0);
        vel += acc * dt;
        pos += vel * dt;

        // Did the step cross the disk plane (y = 0)?
        if (prevPos.y * pos.y < 0.0) {
            float f = prevPos.y / (prevPos.y - pos.y);
            vec3 hit = mix(prevPos, pos, clamp(f, 0.0, 1.0));
            float rr = length(hit.xz);
            if (rr >= kDiskInner && rr <= kDiskOuter) {
                // Two crossings (front/back) plus lensed repeats accumulate.
                col += diskEmission(hit, normalize(vel), iTime, k)
                     * (1.0 - clamp(dot(col, vec3(0.33)), 0.0, 0.85));
            }
        }
        prevY = pos.y;
    }

    // ── background stars (lensing warps the sampled direction) ──
    vec3 bg = vec3(0.004, 0.006, 0.014);
    if (escaped) {
        vec3 d = escapeDir;
        vec2 dir = d.xy / max(abs(d.z), 0.06);
        bg += starField(dir * 1.15, iTime) * stars;
    }
    col += bg * (1.0 - captured);

    // ── photon ring: screen-space, at the critical impact parameter ──
    float ring = exp(-abs(bImpact - bCrit) * 85.0);
    col += mix(vec3(1.0, 0.95, 0.88), vec3(1.0, 0.72, 0.38), 0.45)
         * ring * photon * (0.9 + 0.6 * k) * 0.55;

    // Soft halo just outside the shadow.
    col += mix(vec3(1.0, 0.80, 0.55), vec3(0.55, 0.72, 1.0), palette * 0.4)
         * exp(-abs(bImpact - bCrit) * 5.0) * photon * 0.06;

    // Filmic tonemap keeps the disk white-hot without clipping to a blob.
    col = vec3(1.0) - exp(-col * 1.6);
    col += (hash21(fragCoord + seed * 5.0) - 0.5) * 0.012;
    col *= 1.0 - smoothstep(0.55, 1.25, length(p)) * 0.35;

    fragColor = vec4(col, 1.0);
}
