// Pixelate — snap sampling to a grid of square cells. Classic retro look.
// MIT License.

uniform float cellSize; // @range 2..96 default=16.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 cell = vec2(cellSize);
    vec2 snapped = floor(fragCoord / cell) * cell + cell * 0.5;
    fragColor = texture(iPrev, snapped / iResolution.xy);
}
