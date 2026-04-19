// Diamond-split grid: each cell is cut by its diagonal into two triangles,
// each gets its own phase and colour. Per-cell pulse driven by iBeat.
// MIT License.

uniform float size;     // @range 4..40 default=14
uniform float beatSync; // @range 0..1 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec2 g  = uv * size;
    vec2 gi = floor(g);
    vec2 gf = fract(g);
    float flip = step(gf.x + gf.y, 1.0);     // 1 on the lower-left half

    float id   = dot(gi, vec2(1.0, 57.0)) + flip * 0.5;
    float seed = fract(sin(id * 12.9898) * 43758.5453);

    float pulse  = mix(0.0, fract(iBeat + seed), beatSync);
    float bright = 0.3 + 0.7 * pow(1.0 - pulse, 3.0);

    vec3 col = 0.5 + 0.5 * cos(6.28318 * (seed + vec3(0.0, 0.33, 0.67)));
    col *= bright;
    fragColor = vec4(col, 1.0);
}
