// Horizontal scrolling marquee from the clip's text (iChannel1).
// Right-click the clip → "Set Text…" to change the string.
//
// Outputs alpha=0 outside the glyphs so this shader can be layered on top
// of other visuals without blanking them. Use Normal blend (default).
// MIT License.

uniform float speed;       // @range -3..3 default=0.6
uniform float bandHeight;  // @range 0.05..4 default=0.30
uniform float bandCenterY; // @range 0..1 default=0.5
uniform float textScale;   // @range 0.25..4 default=1.0
uniform float r;           // @range 0..1 default=0.1
uniform float g;           // @range 0..1 default=0.9
uniform float b;           // @range 0..1 default=0.6

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float bandHalf = bandHeight * 0.5;

    float a = 0.0;
    if (abs(uv.y - bandCenterY) < bandHalf) {
        float bandLocal = (uv.y - (bandCenterY - bandHalf)) / bandHeight;
        // Horizontal scale: >1 stretches the glyphs wider (each source
        // pixel covers more screen pixels), <1 squishes. Scroll rate is
        // independent of textScale so "speed" keeps its meaning.
        float sx  = max(textScale, 0.0001);
        float pxX = fragCoord.x / sx + iTime * speed * iTextWidth * 0.5;
        float px  = mod(pxX, max(iTextWidth, 1.0));
        float u   = px / iTextTexWidth;
        float v   = 1.0 - bandLocal;
        a = texture(iChannel1, vec2(u, v)).r;
    }
    fragColor = vec4(vec3(r, g, b) * a, a);
}
