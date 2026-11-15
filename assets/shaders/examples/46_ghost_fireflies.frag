// 46 · Ghost Fireflies — the improv layer made visible: every upcoming
// note the live code has scheduled (iGhost0..7, the same bus the shader
// family already uses) becomes a drifting mote that brightens as its
// fire time approaches and flashes when it lands. Notes from different
// tracks get different colours; the orbit carries the pitch. Alpha
// overlay — screen it over any of the family.
//
// iGhostN = vec4(pitch/127, velocity, beats-until-fire, track).
// MIT License.

uniform float size;    // @range 0.3..2.5 default=1.4  mote size
uniform float orbit;   // @range 0..1   default=0.70 orbital radius spread
uniform float glow;    // @range 0..1   default=1.0
uniform float ring;    // @range 0..1   default=0.55 impact rings
uniform float spin;    // @range -2..2  default=0.18 orbital drift
uniform float palette; // @range 0..1   default=0.40
uniform float seed;    // @range 0..1   default=0.0

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

vec4 ghostAt(int idx) {
    if (idx == 0) return iGhost0;
    if (idx == 1) return iGhost1;
    if (idx == 2) return iGhost2;
    if (idx == 3) return iGhost3;
    if (idx == 4) return iGhost4;
    if (idx == 5) return iGhost5;
    if (idx == 6) return iGhost6;
    return iGhost7;
}

vec3 trackColor(float track, float palette) {
    int t = int(mod(track + 0.5, 4.0));
    vec3 c;
    if (t == 0)      c = vec3(1.00, 0.74, 0.26);   // gold
    else if (t == 1) c = vec3(1.00, 0.36, 0.16);   // ember
    else if (t == 2) c = vec3(0.42, 0.72, 1.00);   // cyan
    else             c = vec3(0.95, 0.40, 0.58);   // rose
    return mix(c, vec3(0.88, 0.70, 0.34), palette * 0.5);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;

    vec3 col = vec3(0.0);
    float alpha = 0.0;
    int count = int(clamp(iGhostCount + 0.5, 0.0, 8.0));

    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        vec4 g = ghostAt(i);
        float pitch = g.x;
        float vel = clamp(g.y, 0.0, 1.0);
        float until = g.z;                       // beats until the fire
        float track = g.w;
        float h = hash11(pitch * 113.0 + track * 17.0 + seed * 7.0);

        // Orbit: radius from the pitch, angle from the hash + slow drift
        // (inner tracks drift one way, outer the other).
        float dir = mod(track, 2.0) < 0.5 ? 1.0 : -1.0;
        float ang = h * TAU + iTime * spin * dir + float(i) * 0.11;
        float rad = (0.12 + 0.38 * orbit) * (0.35 + 0.65 * fract(h * 13.7));
        rad += 0.015 * sin(iTime * 1.3 + h * 9.0);
        vec2 mp = vec2(cos(ang), sin(ang)) * rad;

        float d = length(p - mp);

        // Approach: brighter and larger as the fire beat nears.
        float near = smoothstep(6.0, 0.0, until);
        float flash = smoothstep(0.22, 0.0, until);
        float s = size * (0.55 + 0.45 * vel) * (0.7 + 0.5 * near);

        float core = exp(-d * (85.0 / s)) * (0.35 + 0.65 * near);
        float halo = exp(-d * (15.0 / s)) * 0.45 * (0.25 + near);
        vec3 c = trackColor(track, palette);

        float intensity = (core * 2.2 + halo) * glow
                        * (0.35 + 0.75 * vel) * (0.35 + 0.65 * near);
        col += c * intensity;
        alpha = max(alpha, clamp(intensity, 0.0, 1.0));

        // Impact ring at the fire moment.
        float ringR = 0.01 + 0.055 * (1.0 - flash);
        float ringMask = exp(-abs(d - ringR) * 260.0) * flash * ring;
        col += mix(c, vec3(1.0), 0.35) * ringMask * 1.4;
        alpha = max(alpha, clamp(ringMask, 0.0, 1.0));
    }

    // A very faint constellation line between motes ~2 and 2 apart?
    // (skip: the field reads cleaner without links)

    alpha = clamp(alpha * 1.0, 0.0, 1.0);
    // Premultiplied output (engine layer convention).
    fragColor = vec4(col, alpha);
}
