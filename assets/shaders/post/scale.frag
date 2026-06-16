// Scale — uniform zoom around the frame center. Pixels sampled outside the
// unit square become transparent so zooming out doesn't tile the previous
// stage's edges back into view.
//
// fit=0 (default) zooms freely (>1 magnifies + crops). fit=1 never crops —
// it clamps the zoom so the content only shrinks (stays fully visible with a
// transparent margin), which is handy before a rotate/composite.
// MIT License.

uniform float zoom; // @range 0.25..4 default=1.0
uniform float fit;  // @range 0..1    default=0    1 = never crop (shrink only)

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float z = max(zoom, 0.001);
    if (fit > 0.5) z = min(z, 1.0);          // no crop: only zoom out
    vec2 p = (uv - 0.5) / z + 0.5;

    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) {
        fragColor = vec4(0.0);   // transparent: don't tile, don't blank lower layers
    } else {
        fragColor = texture(iPrev, p);
    }
}
