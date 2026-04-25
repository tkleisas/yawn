// Contrast — linear scaling around mid-grey plus a brightness offset.
// 1.0 = identity contrast, 0 = flat grey, >1 punches highlights and
// shadows apart. Brightness adds/subtracts after the scaling so cuts
// and boosts both stay symmetric around the new mid-point.
// MIT License.

uniform float contrast;   // @range 0..2 default=1.0
uniform float brightness; // @range -0.5..0.5 default=0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = texture(iPrev, uv).rgb;
    c = (c - 0.5) * contrast + 0.5 + brightness;
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
