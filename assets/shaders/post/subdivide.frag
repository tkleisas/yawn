// Subdivide — tiles iPrev into an N×N mosaic. Each cell shows a full
// copy of iPrev scaled to fit, modulated by the source's luminance at
// that cell's position — so the cell brightnesses approximate the
// original image while each cell shows a tiny copy of it. cells=1 is
// identity (no subdivision); higher values give finer grids.
// MIT License.

uniform float cells; // @range 1..32 default=1

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float n = max(1.0, floor(cells));

    // Identity at the lowest setting so the effect doesn't dim the
    // source when "off" (pure mosaic with n=1 would tint everything
    // by the center pixel's luminance — surprising at min position).
    if (n <= 1.0) {
        fragColor = texture(iPrev, uv);
        return;
    }

    vec2 cellIdx = floor(uv * n);
    vec2 tileUV  = fract(uv * n);

    // The cell content — a full copy of iPrev scaled to fit the cell.
    vec3 tile = texture(iPrev, tileUV).rgb;

    // Luminance modulator — sample iPrev at the cell's center. At
    // high subdivision counts the cell is small enough that the
    // center sample ≈ cell average, so the grid of tints
    // approximates the original image.
    vec2 cellCenter = (cellIdx + 0.5) / n;
    vec3 src = texture(iPrev, cellCenter).rgb;
    float lum = dot(src, vec3(0.299, 0.587, 0.114));

    fragColor = vec4(tile * lum, 1.0);
}
