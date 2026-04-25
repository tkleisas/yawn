// Scale — uniform zoom around the frame center. Pixels sampled outside
// the unit square clamp to black so zooming out doesn't tile the
// previous stage's edges back into view.
// MIT License.

uniform float zoom; // @range 0.25..4 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (uv - 0.5) / max(zoom, 0.001) + 0.5;

    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        fragColor = texture(iPrev, p);
    }
}
