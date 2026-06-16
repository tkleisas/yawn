// Internal display shader for static-image visual sources. Aspect-fits the
// image (iChannel2) into the layer, letterboxing the remainder with full
// transparency, with a `scale` knob to zoom further. PNG alpha is preserved
// (premultiplied output) so transparent images layer correctly. Normal blend.

uniform float scale;   // @range 0.1..4 default=1.0   size / zoom
uniform float fit;     // @range 0..1   default=0     0 = zoom (>1 crops in), 1 = resize (always fully visible)

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;            // 0..1

    // Scale about the centre. fit=0 zooms (>1 magnifies + crops the image);
    // fit=1 keeps the whole image visible (clamps the magnify so it never
    // crops — only shrinks it, leaving a transparent margin).
    float s = max(scale, 0.0001);
    if (fit > 0.5) s = min(s, 1.0);
    uv = (uv - 0.5) / s + 0.5;

    // Aspect-fit: preserve the image's aspect, centre it, letterbox the rest.
    float imgA = iChannelResolution[2].x / max(iChannelResolution[2].y, 1.0);
    float scrA = iResolution.x / max(iResolution.y, 1.0);
    vec2 iuv = uv;
    if (imgA > scrA) iuv.y = (uv.y - 0.5) * (imgA / scrA) + 0.5;  // bars top/bottom
    else             iuv.x = (uv.x - 0.5) * (scrA / imgA) + 0.5;  // bars left/right

    // GL origin is bottom-left; flip V so the (top-down) image is upright.
    vec2 tuv = vec2(iuv.x, 1.0 - iuv.y);

    if (tuv.x < 0.0 || tuv.x > 1.0 || tuv.y < 0.0 || tuv.y > 1.0) {
        fragColor = vec4(0.0);                       // transparent letterbox
    } else {
        vec4 c = texture(iChannel2, tuv);
        fragColor = vec4(c.rgb * c.a, c.a);          // premultiplied (preserve alpha)
    }
}
