// FFT spectrum bars from iChannel0 row 0. Classic "club visualiser".
// MIT License.

uniform float bars;      // @range 16..256 default=64
uniform float height;    // @range 0.1..1.0 default=0.7
uniform float glow;      // @range 0..4 default=1.5
uniform float tilt;      // @range 0..1 default=0.0

vec3 heat(float t) {
    return clamp(vec3(t * 1.6, pow(t, 1.5), pow(t, 3.0)), 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float x = uv.x;
    float bi = floor(x * bars) + 0.5;
    float binCenter = bi / bars;
    float mag = texture(iChannel0, vec2(binCenter, 0.25)).x;
    mag = pow(mag, 1.0 - tilt * 0.4);

    float barH = mag * height;
    float inside = step(uv.y, barH);

    vec3 col = heat(mag);
    // Soft glow above the bar
    float above = smoothstep(barH + 0.2 * mag, barH, uv.y) - inside;
    col = col * inside + col * glow * above * 0.4;
    fragColor = vec4(col, 1.0);
}
