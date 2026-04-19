// Invert — 1 - color. Cheap, dramatic, great on a strobe-adjacent sync.
// MIT License.

uniform float amount; // @range 0..1 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture(iPrev, uv).rgb;
    fragColor = vec4(mix(col, vec3(1.0) - col, amount), 1.0);
}
