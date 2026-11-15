// Infinite Zoom — recursive feedback zoom of the running composite.
// Samples the previous frame's chain output (iFeedback) scaled and
// rotated slightly, then keeps the brighter of (current, zoomed echo):
// for a scale below 1 the last frame's centre fills this frame a little
// larger, so the image keeps flowing out of (or into) the centre
// forever — the Droste/infinite-corridor look. Radial RGB split and an
// edge fade keep the recursion readable instead of smearing; the kick
// boosts the recursion so the tunnel pumps with the low end.
//
// Chain after rotate/scale (and before bloom) for the classic tunnel:
//   rotate → scale → infinite_zoom → bloom
//
// Engine note: declares iFeedback, which opts the layer into the
// persistent previous-frame copy. Not deterministic by nature (it
// accumulates), which is the point. MIT License.

uniform float speed;  // @range -2..2  default=0.35 zoom in (+) / out (-) per second
uniform float spin;   // @range -1..1  default=0.03 rotation per second
uniform float echo;   // @range 0..0.98 default=0.90 recursion strength
uniform float chroma; // @range 0..1   default=0.25 radial RGB split per recursion
uniform float fold;   // @range 0..1   default=0.0  mirrored-tile sampling
uniform float kick;   // @range 0..1   default=0.35 kick boost to the recursion

const float TAU = 6.283185307179586;

vec2 mirrorTile(vec2 q) {
    q = mod(q, 2.0);
    return mix(q, 2.0 - q, step(1.0, q));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    // Framerate-independent step (clamped against tab-out hitches, where
    // iTimeDelta can be huge and would teleport the recursion).
    float dt = clamp(iTimeDelta, 1.0 / 240.0, 1.0 / 15.0);

    // Centre + rotate + shrink the sampling position. speed > 0 → s < 1
    // → zoom in; speed < 0 → zoom out.
    vec2 p = uv - 0.5;
    float ang = spin * dt;
    float ca = cos(ang), sa = sin(ang);
    p = mat2(ca, -sa, sa, ca) * p;
    float s = exp(-speed * dt);
    vec2 q = 0.5 + p * s;
    q = mix(q, mirrorTile(q), fold);

    vec4 cur = texture(iPrev, uv);

    // Fade the echo near the texture borders (clamped sampling would
    // otherwise smear the edges inward as streaks).
    float inside = smoothstep(0.0, 0.045,
        min(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)));

    // Radial RGB split: each channel samples a step further out, so
    // every recursion generation separates a little more.
    vec2 dir = q - 0.5;
    float caOff = chroma * 0.02;
    vec3 fb;
    fb.r = texture(iFeedback, q + dir * caOff).r;
    fb.g = texture(iFeedback, q).g;
    fb.b = texture(iFeedback, q - dir * caOff).b;
    float fbA = texture(iFeedback, q).a;
    fb *= echo * inside * (1.0 + kick * clamp(iKick, 0.0, 1.0) * 1.2);
    fbA *= echo * inside;

    // max() keeps the current frame crisp while the recursion shows
    // through — additive or mix would bloom toward white over time.
    fragColor = vec4(max(cur.rgb, fb), max(cur.a, fbA));
}
