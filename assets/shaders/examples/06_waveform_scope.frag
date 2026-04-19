// Oscilloscope-style waveform line from iChannel0 row 1.
// MIT License.

uniform float gain;       // @range 0.1..4 default=1.0
uniform float thickness;  // @range 0.001..0.05 default=0.008
uniform float glow;       // @range 0..8 default=3.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float s = texture(iChannel0, vec2(uv.x, 0.75)).x;
    float y = 0.5 + (s - 0.5) * gain;
    float d = abs(uv.y - y);
    float line = smoothstep(thickness, 0.0, d);
    float halo = 1.0 / (1.0 + d * d * 200.0);
    vec3 col = vec3(0.1, 1.0, 0.6) * line + vec3(0.05, 0.4, 0.25) * halo * glow;
    fragColor = vec4(col, 1.0);
}
