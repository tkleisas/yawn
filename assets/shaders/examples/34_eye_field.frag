// 34 · Eye Field — a ring of watching eyes, the family's "many eyes"
// overlay: almond eyes on concentric lattices, each iris coloured from
// the seed picture at its own sample point, gold-rimmed on parchment
// sclera. Kicks make the eyes blink (and the pupils flare); the rings
// counter-rotate slowly with the beat. Premultiplied alpha, so it
// composites over the mandala/frame with the normal or screen blend —
// the area between eyes stays transparent. Deterministic per picture +
// knobs. MIT License.

uniform float eyes;    // @range 4..16 default=8   eyes on the first ring
uniform float rings;   // @range 1..3  default=2   concentric lattices
uniform float radius;  // @range 0.16..0.5 default=0.27 first-ring radius
uniform float spread;  // @range 0..0.16 default=0.095 ring spacing
uniform float size;    // @range 0.05..0.16 default=0.078 eye half-width
uniform float open;    // @range 0..1  default=1.0  eyelid openness
uniform float gold;    // @range 0..1  default=0.80 gold rims + lashes
uniform float palette; // @range 0..1  default=0.35 0=picture 1=traditional
uniform float spin;    // @range -1..1 default=0.05 ring rotation
uniform float seed;    // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash11(float x) {
    return fract(sin(x * 127.1 + 311.7) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);
    float a = atan(p.y, p.x);

    // ── seed picture probes: palette + per-eye sampling ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));
    float hi = max(max(avg.r, avg.g), avg.b);
    float lo = min(min(avg.r, avg.g), avg.b);
    float h = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                        vec3(12.9898, 78.233, 37.719)) * 43758.5453
                    + seed * 0.6180339);

    // Palette (family convention): 0 = picture-derived, 1 = traditional.
    vec3 imgPaper  = clamp(avg * 1.15 + lum * 0.35 + 0.10, 0.0, 1.0);
    vec3 imgGold   = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 imgAccent = clamp(mix(s2, s4, 0.5) * 1.5 + 0.05, 0.0, 1.0);
    vec3 imgDeep   = avg * 0.16 + vec3(0.012, 0.014, 0.028);
    vec3 colPaper  = mix(imgPaper,  vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold   = mix(imgGold,   vec3(0.880, 0.660, 0.250), palette);
    vec3 colAccent = mix(imgAccent, vec3(0.540, 0.130, 0.095), palette);
    vec3 colDeep   = mix(imgDeep,   vec3(0.030, 0.045, 0.110), palette);

    float kick = clamp(iKick, 0.0, 1.0);
    float ringCount = floor(rings + 0.5);
    float eyeCount0 = floor(eyes + 0.5);

    vec3 col = vec3(0.0);
    float alpha = 0.0;

    // Evaluate only the nearest eye of each ring (eyes sit on a regular
    // angular lattice, so the cell index is analytic).
    for (int k = 0; k < 3; ++k) {
        if (float(k) >= ringCount) break;
        float rk = radius + float(k) * spread;
        float ek = max(4.0, eyeCount0 + float(k) * 2.0);
        float stepA = TAU / ek;
        float off = h * TAU + iTime * spin * (1.0 - 0.25 * float(k))
                  + iBeat * 0.012 * (1.0 + 0.5 * float(k))
                  + float(k) * 0.37 + seed * 1.7;
        // Nearest lattice cell — measured in the SHIFTED lattice (the
        // offset must be removed before rounding, or every eye is culled).
        float jf = floor((a - off) / stepA + 0.5);
        float theta = jf * stepA + off;
        float hj = hash11(jf * 1.73 + float(k) * 7.31 + h * 5.0);

        // Coarse cull: this pixel is near the ring and eye size.
        vec2 c = rk * vec2(cos(theta), sin(theta));
        float dEye = length(p - c);
        float eyeR = size * 1.6;
        if (dEye > eyeR) continue;

        // Eye-local frame: x along the ring (tangent), y radial.
        vec2 rel = p - c;
        vec2 tang = vec2(-sin(theta), cos(theta));
        vec2 radi = vec2(cos(theta), sin(theta));
        vec2 q = vec2(dot(rel, tang), dot(rel, radi)) / size;
        float aa = max(fwidth(q.x), 0.01);

        // Eyelid: almond profile; kicks blink, plus a slow per-eye drift.
        float openAmt = open * (0.92 + 0.08 * sin(iTime * 0.6 + hj * TAU));
        openAmt *= 1.0 - 0.90 * kick;
        openAmt = max(openAmt, 0.04);
        float lid = abs(q.y) - (1.0 - q.x * q.x) * 0.42 * openAmt;
        float inX = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, abs(q.x));
        float inside = (1.0 - smoothstep(-aa * 0.8, aa * 0.8, lid)) * inX;

        if (inside > 0.001) {
            // Sclera: parchment with a soft upper shading.
            vec3 sclera = colPaper * (0.72 + 0.28 * openAmt)
                        * (0.86 + 0.14 * clamp(-q.y * 2.0, 0.0, 1.0));

            // Iris: picture colour at this eye's own sample point.
            vec2 iuv = vec2(fract(h * 0.71 + jf * 0.219 + float(k) * 0.173),
                            fract(h * 0.37 + jf * 0.137 + float(k) * 0.291));
            vec3 irisBase = texture(iChannel2, iuv).rgb;
            irisBase = mix(irisBase, colAccent, 0.30)
                     * (0.55 + 0.45 * (0.5 + 0.5 * hj));
            float irid = length(q);
            float irisR = 0.40 + 0.03 * hj;
            float irisMask = 1.0 - smoothstep(irisR - aa, irisR + aa, irid);
            float ang = atan(q.y, q.x);
            float fibers = 0.5 + 0.5 * sin(ang * 20.0 + hj * TAU * 3.0);
            irisBase *= 0.72 + 0.40 * fibers;

            // Pupil: deep, with a kick flare and a fixed catchlight.
            float pupilR = 0.17 * (1.0 + 0.35 * kick);
            float pupil = 1.0 - smoothstep(pupilR - aa, pupilR + aa, irid);
            vec3 pupilCol = mix(colDeep, colAccent * 0.5, 0.25 * kick);
            float catchl = 1.0 - smoothstep(0.10 - aa, 0.10 + aa,
                                      length(q - vec2(-0.14, 0.16)));
            catchl = max(catchl, (1.0 - smoothstep(0.05 - aa, 0.05 + aa,
                          length(q - vec2(0.10, -0.12)))) * 0.5);

            vec3 eye = mix(sclera, irisBase, irisMask);
            eye = mix(eye, pupilCol, pupil);
            eye += colPaper * catchl * 0.9;

            // Gold rim along the lid + a faint lash shadow.
            float rim = 1.0 - smoothstep(0.0, aa * 2.4, abs(lid));
            rim *= inX;
            eye = mix(eye, colGold, rim * gold);
            eye += colGold * rim * gold * 0.35;

            col += eye * inside;
            alpha = max(alpha, inside);
        }
    }

    // A faint aura between the rings so the lattice reads as one field.
    float field = smoothstep(radius - size * 2.0, radius + size * 2.0, r) *
                  (1.0 - smoothstep(radius + spread * ringCount,
                                    radius + spread * ringCount + 0.1, r));
    col += colGold * field * 0.015 * (0.5 + 0.5 * kick);
    alpha = max(alpha, field * 0.05);

    alpha = clamp(alpha, 0.0, 1.0);
    // Premultiplied output (engine layer convention).
    fragColor = vec4(col * alpha, alpha);
}
