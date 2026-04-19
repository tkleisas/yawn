// Classic sine plasma — four overlapping sinusoids, palette-mapped.
// MIT License.
//
// @range comments drive YAWN's parameter panel (Phase C).

uniform float speed;     // @range 0..4 default=1.0
uniform float scale;     // @range 0.2..8 default=2.0
uniform float warp;      // @range 0..2 default=0.4

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    uv *= scale;

    float t = iTime * speed;
    float v = 0.0;
    v += sin(uv.x + t);
    v += sin(uv.y * 0.9 + t * 1.2);
    v += sin(length(uv) * 3.0 - t);
    v += sin((uv.x + uv.y) * 0.7 + t * 0.7);
    v *= 0.25;

    // Warp the coords by v for a "melty" feel, then re-sample.
    uv += warp * vec2(sin(v * 3.14), cos(v * 3.14));
    v = 0.5 + 0.5 * sin(v * 3.14159 + iTime);

    vec3 col = 0.5 + 0.5 * cos(6.28318 * (v + vec3(0.0, 0.33, 0.67)));
    col *= 0.7 + 0.3 * iAudioLevel;

    fragColor = vec4(col, 1.0);
}
