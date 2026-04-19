// Text pipeline debug — samples iChannel1 directly across the screen.
// Load this on a visual track BY ITSELF (or as the only visible layer)
// and Set Text. You should see large white text on black.
//
// If this is solid black, the text texture isn't bound.
// If you see the text clipped to the left ~8% of screen, the upload worked
// but the shader is wrong. The "fit" version below stretches the used
// text area to fill the whole viewport.
// MIT License.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Stretch the filled region of the texture across the viewport so the
    // glyphs are easy to see regardless of text length.
    float fill = iTextWidth / iTextTexWidth;   // e.g. 0.08 for short text
    float u    = uv.x * fill;
    float v    = 1.0 - uv.y;
    float a    = texture(iChannel1, vec2(u, v)).r;
    fragColor  = vec4(vec3(a), 1.0);
}
