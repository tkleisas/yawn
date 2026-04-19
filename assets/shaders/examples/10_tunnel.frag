// Infinite tunnel — polar coords + log-depth stripes, kick squeezes radius.
// MIT License.

uniform float speed;    // @range 0..4 default=1.0
uniform float twist;    // @range -3..3 default=0.5
uniform float stripes;  // @range 1..20 default=8

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    float squeeze = 1.0 - iKick * 0.3;
    float z = 1.0 / max(r * squeeze, 0.05);
    float angle = a + z * twist * 0.1;

    float t = iTime * speed;
    float depth = fract(z * 0.2 - t);
    float ring  = smoothstep(0.0, 0.02, depth) - smoothstep(0.8, 1.0, depth);

    float seg = floor(angle * stripes / 3.14159);
    float shade = 0.5 + 0.5 * sin(seg + t);

    vec3 col = vec3(0.1, 0.3, 0.5) * ring + vec3(0.9, 0.6, 0.3) * shade * ring;
    col *= smoothstep(0.02, 0.5, r);
    fragColor = vec4(col, 1.0);
}
