// Pure kick-reactive flash — minimal shader that just flashes on iKick.
// Works brilliantly as a top Screen-blend layer.
// MIT License.

uniform float r;       // @range 0..1 default=1.0
uniform float g;       // @range 0..1 default=0.9
uniform float b;       // @range 0..1 default=0.7
uniform float falloff; // @range 0..4 default=1.4

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float fall = exp(-length(uv) * falloff);
    vec3 col = vec3(r, g, b) * iKick * fall;
    fragColor = vec4(col, 1.0);
}
