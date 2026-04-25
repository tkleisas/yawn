// RGB — per-channel gain. Multiply each colour channel independently
// to cut (0) or boost (>1) red, green, blue. 1.0 = identity. Useful
// for fast colour-grading or to isolate a channel before chaining
// into bloom / edge / echo for one-colour glow / outline effects.
// MIT License.

uniform float r; // @range 0..2 default=1.0
uniform float g; // @range 0..2 default=1.0
uniform float b; // @range 0..2 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = texture(iPrev, uv).rgb;
    fragColor = vec4(c.r * r, c.g * g, c.b * b, 1.0);
}
