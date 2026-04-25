// Mirror — flips iPrev horizontally and/or vertically. Both flips can
// be enabled together to rotate 180°. Each toggle treated as a switch
// (>0.5 = on) so a hardware encoder bumped past the midpoint snaps
// cleanly between states.
// MIT License.

uniform float flip_h; // @range 0..1 default=0
uniform float flip_v; // @range 0..1 default=0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    if (flip_h > 0.5) uv.x = 1.0 - uv.x;
    if (flip_v > 0.5) uv.y = 1.0 - uv.y;
    fragColor = texture(iPrev, uv);
}
