// Centered text that pulses on every kick. Outputs alpha so the layer
// below shows through everywhere except on glyph pixels — try stacking
// this on top of a plasma (01) layer.
// MIT License.

uniform float pulseScale;  // @range 0..1 default=0.25
uniform float size;        // @range 0.3..2 default=1.0

vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Kick-scaled zoom.
    float scale = size * (1.0 + iKick * pulseScale);
    vec2  p = (uv - 0.5) / scale + 0.5;

    // Fit text centred in a strip ~40% of screen height.
    float stripH = 0.4;
    float texAspect = iTextWidth / iChannelResolution[1].y;
    float textScreenW = stripH * texAspect * (iResolution.y / iResolution.x);
    float u0 = (p.x - 0.5) / textScreenW + 0.5;
    float fill = iTextWidth / iTextTexWidth;
    float u  = u0 * fill;
    float v  = 1.0 - ((p.y - (0.5 - stripH * 0.5)) / stripH);

    float a = 0.0;
    if (u >= 0.0 && u <= fill && v >= 0.0 && v <= 1.0) {
        a = texture(iChannel1, vec2(u, v)).r;
    }

    vec3 fg = palette(iTime * 0.3 + 0.5) + iKick * 0.4;
    fragColor = vec4(fg * a, a);
}
