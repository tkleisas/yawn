// Noise — additive grain overlay. `amount` controls how much noise is
// blended on top of iPrev; `color` blends between monochrome film
// grain (0) and full-colour static (1); `freeze` snaps the noise to a
// stationary pattern (0 = animated at 30Hz, 1 = held). Drop this
// after a colour-grade pass to dirty things up before bloom/echo.
// MIT License.

uniform float amount; // @range 0..0.5 default=0.1
uniform float color;  // @range 0..1   default=0
uniform float freeze; // @range 0..1   default=0

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c  = texture(iPrev, uv).rgb;

    // Animation step at 30Hz so the grain reads as motion-picture
    // texture, not a smooth blur. freeze >= 0.5 latches the seed.
    float t = (freeze > 0.5) ? 0.0 : floor(iTime * 30.0);
    vec2  p = fragCoord;

    float nMono = hash(p + vec2(t)) - 0.5;
    vec3  nCol  = vec3(hash(p + vec2(t,        0.0)),
                        hash(p + vec2(0.0,      t)),
                        hash(p + vec2(t * 0.7,  t * 1.3))) - 0.5;
    vec3  n     = mix(vec3(nMono), nCol, color);

    fragColor = vec4(clamp(c + n * amount * 2.0, 0.0, 1.0), 1.0);
}
