// Subdivide — tiles iPrev into an N×N mosaic, each cell a full copy of iPrev
// scaled to fit. `modulate` chooses what the cells do:
//   1 (default) → each cell is tinted by the source's colour/luminance at that
//                 cell's position, so the grid of tints approximates the
//                 original image (a mosaic "made of" tiny copies of itself).
//   0           → each cell is a plain, unaltered copy — no recolouring.
// cells=1 is identity (no subdivision); higher values give finer grids.
// MIT License.

uniform float cells;    // @range 1..32 default=1
uniform float modulate; // @range 0..1  default=1   tint cells by the source (1) vs plain copy (0)

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float n = max(1.0, floor(cells));

    // Identity at the lowest setting so the effect doesn't recolour the
    // source when "off" (a pure 1×1 mosaic would tint everything by the
    // centre pixel — surprising at the min position).
    if (n <= 1.0) {
        fragColor = texture(iPrev, uv);
        return;
    }

    vec2 tileUV = fract(uv * n);

    // The cell content — a full copy of iPrev scaled to fit the cell.
    vec4 tile = texture(iPrev, tileUV);

    // Tint by the source's colour at the cell centre. At high subdivision
    // counts the cell is small enough that the centre sample ≈ the cell
    // average, so the grid of tints approximates the original image.
    vec2 cellCenter = (floor(uv * n) + 0.5) / n;
    vec3 src  = texture(iPrev, cellCenter).rgb;
    vec3 tint = mix(vec3(1.0), src, clamp(modulate, 0.0, 1.0));

    fragColor = vec4(tile.rgb * tint, tile.a);
}
