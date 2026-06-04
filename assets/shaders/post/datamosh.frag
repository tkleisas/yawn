// Datamosh — block-displacement + frozen-frame smear: the dropped-P-frame
// look. The image tears into blocks that slide and melt; the kick punches
// big jumps. Always visibly active (a continuous floor) and reactive on top.
//
// Trails: this samples iFeedback (the previous chain output) for the melt.
// As a PER-TRACK effect the engine feeds a real previous-frame texture, so
// you get accumulating smears. In the global View → Post FX chain iFeedback
// isn't wired, so you still get the block tearing — just without the long
// melting trails. Preserves iPrev alpha so it stays layerable. MIT License.

uniform float amount;    // @range 0..1   default=0.6   // base activity / displacement
uniform float blockSize; // @range 4..128 default=24    // block edge in px
uniform float kick;      // @range 0..2   default=1.0   // kick-driven jump amount
uniform float freeze;    // @range 0..1   default=0.6   // melt/trail (per-track feedback)

uniform sampler2D iFeedback;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
vec2 hash22(vec2 p) {
    float n = hash21(p);
    return vec2(n, hash21(p + n));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv    = fragCoord / iResolution.xy;
    vec2 grid  = iResolution.xy / max(blockSize, 1.0);
    vec2 block = floor(uv * grid);

    // Each block re-decides ~8x/sec so the mosh reads as discrete frame
    // drops, not a smooth wobble.
    float stp = floor(iTime * 8.0);
    vec2  rnd = hash22(block + stp * 1.7);

    // Solid continuous floor (always visibly moshing) + a big kick spike,
    // driving both how many blocks tear and how far they jump.
    float drive = clamp(amount * (0.5 + 1.5 * iKick * kick), 0.0, 1.0);
    float gate  = step(1.0 - drive, rnd.x);

    // Displacement (uv space) for glitching blocks — clearly visible even at
    // the floor, huge on the kick.
    vec2 disp = (rnd - 0.5) * (0.06 + 0.35 * drive) * gate;

    vec4 cur  = texture(iPrev, uv + disp);          // current frame, torn
    vec4 mosh = texture(iFeedback, uv + disp * 1.5); // feedback, shoved → melt

    // freeze pushes glitching blocks toward the accumulating feedback for
    // long smears (per-track chain only; harmless in the global chain).
    float melt = gate * mix(0.25, 0.9, freeze);
    fragColor = mix(cur, mosh, melt);
}
