// 37 · Infinite Zoom — a deterministic endless-zoom lattice built from
// the seed picture. The image is sampled in log-polar space: the radius
// becomes a wrapped, mirrored ladder (so the picture repeats forever at
// exponentially spaced scales) and the angle is folded N ways like the
// rest of the seed family. No feedback needed — the same frame at time
// t is identical every run — so this composes with the mandala/frame/
// eyes as a luminous alpha overlay (premultiplied), or alone as a
// background when blended additively. Kick pumps the glow; the wheel
// turns with speed. MIT License.

uniform float folds;   // @range 2..16 default=8    angular fold count
uniform float speed;   // @range -2..2 default=0.25 zoom-flow speed
uniform float scales;  // @range 1..8  default=3.0  image repeats per log span
uniform float twist;   // @range -2..2 default=0.35 spiral twist with depth
uniform float palette; // @range 0..1  default=0.35 0=picture 1=traditional
uniform float glow;    // @range 0..1  default=0.60 ring-ridge glow
uniform float seed;    // @range 0..1  default=0.0  alternate take

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);
    float th = atan(p.y, p.x);
    float lr = log(max(r, 1e-4));

    // ── seed picture probes: palette + phase ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s1 = texture(iChannel2, vec2(0.18, 0.24)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 s4 = texture(iChannel2, vec2(0.74, 0.77)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3 + s4) * 0.2;
    float lumAvg = dot(avg, vec3(0.299, 0.587, 0.114));
    float hh = fract(dot(avg + s2 * 0.31 + s3 * 0.17,
                         vec3(12.9898, 78.233, 37.719)) * 43758.5453
                     + seed * 0.6180339);

    vec3 imgPaper  = clamp(avg * 1.15 + lumAvg * 0.35 + 0.10, 0.0, 1.0);
    vec3 imgGold   = clamp(avg * vec3(1.45, 1.15, 0.55) + 0.16, 0.0, 1.0);
    vec3 colDeep   = mix(avg * 0.16 + vec3(0.012, 0.014, 0.028),
                         vec3(0.030, 0.045, 0.110), palette);
    vec3 colPaper  = mix(imgPaper, vec3(0.910, 0.840, 0.650), palette);
    vec3 colGold   = mix(imgGold,  vec3(0.880, 0.660, 0.250), palette);

    float kick = clamp(iKick, 0.0, 1.0);

    // ── folded angle + wrapped log-radius ladder ──
    float seg = PI / floor(folds + 0.5);
    float au = mod(th + twist * lr + hh * TAU * 0.3, 2.0 * seg);
    au = abs(au - seg);                       // 0..seg (mirrored wedge)
    float zl = lr * max(scales, 0.5) * 1.6 - iTime * speed + hh * 0.7;
    float sy = abs(fract(zl) * 2.0 - 1.0);    // mirrored zoom ladder
    float sx = abs(au / seg * 2.0 - 1.0);     // mirrored across the wedge

    // The picture, sampled through the ladder — repeats forever.
    vec3 img = texture(iChannel2, vec2(sx, sy)).rgb;
    float lum = dot(img, vec3(0.299, 0.587, 0.114));

    // Family palette: picture-derived, pulled toward lapis/gold/ink.
    vec3 col = mix(img * 1.05, mix(colDeep * 1.4, colGold,
                                   smoothstep(0.10, 0.85, lum)),
                   0.30 + 0.50 * palette);

    // Gold ridge line where the ladder wraps (each zoom generation edge).
    float ridge = smoothstep(0.90, 1.0, sy);
    col += colGold * ridge * glow * (0.5 + 0.5 * kick);
    col += colPaper * ridge * 0.10;

    // Angular filaments so the fold seams read as rays.
    float seam = 1.0 - smoothstep(0.0, 0.035, au / seg);
    col += colGold * seam * glow * 0.35;

    // Central bindu: hides the log singularity and anchors the wheel.
    float core = 1.0 - smoothstep(0.02, 0.055, r);
    col = mix(col, mix(colDeep, colGold, 0.55), core * 0.85);
    col += colGold * core * 0.25 * (0.6 + 0.4 * kick);

    // Coverage: an annulus that ignores the singularity and fades out
    // toward the frame, weighted by brightness so it overlays as light.
    float cov = smoothstep(0.015, 0.05, r) *
                (1.0 - smoothstep(0.48, 0.62, r));
    cov *= 0.22 + 0.78 * smoothstep(0.08, 0.7, lum);
    cov = max(cov, core * 0.9);

    cov = clamp(cov, 0.0, 1.0);
    // Premultiplied output (engine layer convention).
    fragColor = vec4(col * cov, cov);
}
