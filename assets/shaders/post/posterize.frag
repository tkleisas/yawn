// Posterize — quantize each colour channel into N discrete levels for
// a flat, poster-print look. levels=2 gives stark cartoon banding
// (8 colours total), higher values approach the original image.
// Pairs nicely after edge for a comic-strip effect.
// MIT License.

uniform float levels; // @range 2..32 default=8

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c  = texture(iPrev, uv).rgb;

    float n = max(2.0, floor(levels));
    // floor(c * (n-1) + 0.5) / (n-1) keeps both 0.0 and 1.0
    // representable, so a fully white pixel stays white at any level
    // count instead of darkening towards (n-1)/n.
    c = floor(c * (n - 1.0) + 0.5) / (n - 1.0);

    fragColor = vec4(c, 1.0);
}
