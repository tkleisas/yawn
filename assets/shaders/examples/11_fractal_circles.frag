// Recursive circle packing via domain folding — cheap Mandelbrot-adjacent.
// MIT License.

uniform float iters;    // @range 2..8 default=5
uniform float zoom;     // @range 0.5..4 default=1.5
uniform float drift;    // @range 0..2 default=0.3

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * zoom;
    uv += drift * vec2(sin(iTime * 0.3), cos(iTime * 0.4));

    float acc = 0.0;
    vec2 p = uv;
    int n = int(clamp(iters, 1.0, 8.0));
    for (int i = 0; i < 8; i++) {
        if (i >= n) break;
        p = abs(p) / dot(p, p) - 1.0;
        acc += exp(-dot(p, p) * 4.0);
    }
    acc /= float(n);

    vec3 col = 0.5 + 0.5 * cos(6.28318 * (acc + vec3(0.0, 0.33, 0.67)));
    col *= acc * 1.8;
    fragColor = vec4(col, 1.0);
}
