// Echo / Feedback — superimposes a decaying copy of the previous
// frame's final chain output over the current iPrev. Like an audio
// delay's wet signal: the current frame plays at full strength while
// the previous frame's echo decays. Chain after rotate / scale /
// mirror and the trails inherit the transformation, so the echo
// spins or zooms as it fades — classic VJ feedback look.
//
// Engine note: requires `iFeedback` (a persistent per-layer texture
// holding the previous frame's chain output). The engine allocates
// + populates it lazily — declaring this uniform is what opts the
// layer into the per-frame copy. decay=0 collapses to a passthrough.
// MIT License.

uniform float decay; // @range 0..0.98 default=0.85

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 cur  = texture(iPrev, uv).rgb;
    vec3 prev = texture(iFeedback, uv).rgb;
    // max() keeps the current frame at full strength while showing
    // wherever the decaying echo is brighter — gives crisp, punchy
    // trails. (mix() would smear the current frame into the trail
    // and fade everything together, less echo-like.)
    fragColor = vec4(max(cur, prev * decay), 1.0);
}
