// Circular EQ — radial bars pulled from iChannel0 spectrum.
// MIT License.

uniform float bars;     // @range 16..256 default=96
uniform float inner;    // @range 0.05..0.5 default=0.22
uniform float length_;  // @range 0.05..0.5 default=0.25
uniform float glow;     // @range 0..4 default=1.2

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    float t = (a + 3.14159) / 6.28318;  // 0..1
    float bi = floor(t * bars) + 0.5;
    float b = bi / bars;
    float mag = texture(iChannel0, vec2(b, 0.25)).x;

    float innerR = inner;
    float outerR = inner + length_ * mag;
    float inRing = step(innerR, r) * step(r, outerR);

    // Angular per-bar mask so you can count the bars visually.
    float frac = fract(t * bars);
    float gap = step(0.08, min(frac, 1.0 - frac));

    vec3 col = vec3(1.0, 0.7, 0.2) * mag + vec3(0.1, 0.4, 1.0) * (1.0 - mag);
    col *= inRing * gap;
    col += col * glow * smoothstep(outerR + 0.05, outerR, r) * 0.4 * inRing;
    fragColor = vec4(col, 1.0);
}
