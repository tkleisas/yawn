// Vignette — darken the frame toward the edges.
// MIT License.

uniform float strength; // @range 0..2 default=0.7
uniform float softness; // @range 0.1..1 default=0.5

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture(iPrev, uv).rgb;
    float d = distance(uv, vec2(0.5));
    float vign = 1.0 - smoothstep(0.5 - softness * 0.5, 0.8, d) * strength;
    fragColor = vec4(col * vign, 1.0);
}
