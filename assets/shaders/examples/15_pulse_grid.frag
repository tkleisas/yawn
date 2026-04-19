// Grid of cells that pulse on kick detection. Perfect with a tight kick.
// MIT License.

uniform float cells;    // @range 4..32 default=10
uniform float spread;   // @range 0..1 default=0.6
uniform float decay;    // @range 0.5..6 default=3.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 g = uv * cells;
    vec2 gi = floor(g);
    vec2 gf = fract(g) - 0.5;

    float d = length(gf);
    float seed = fract(sin(dot(gi, vec2(12.9898, 78.233))) * 43758.5453);

    // Each cell has a random per-kick delay, controlled by "spread", and
    // fades out exponentially afterwards.
    float delay = seed * spread;
    float pulse = iKick * exp(-delay * decay);

    float mask = smoothstep(0.45, 0.1, d);
    vec3 baseCol = 0.5 + 0.5 * cos(6.28318 * (seed + vec3(0.1, 0.5, 0.8)));
    vec3 col = baseCol * mask * (0.25 + pulse * 2.0);
    fragColor = vec4(col, 1.0);
}
