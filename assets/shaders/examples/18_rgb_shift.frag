// RGB channel shift + scanlines over a cosine gradient. Audio-reactive
// shift distance so the tearing pulses with the track.
// MIT License.

uniform float shift;     // @range 0..0.05 default=0.008
uniform float scanline;  // @range 0..1 default=0.25
uniform float reactive;  // @range 0..2 default=1.0

vec3 palette(vec2 uv) {
    float t = iTime * 0.3 + uv.x * 1.2 + uv.y * 0.6;
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float s = shift * (1.0 + reactive * iAudioLow * 3.0);

    float r = palette(uv + vec2(s, 0.0)).r;
    float g = palette(uv).g;
    float b = palette(uv - vec2(s, 0.0)).b;
    vec3 col = vec3(r, g, b);

    float line = 0.5 + 0.5 * sin(uv.y * iResolution.y * 1.0);
    col *= 1.0 - scanline * (1.0 - line);
    fragColor = vec4(col, 1.0);
}
