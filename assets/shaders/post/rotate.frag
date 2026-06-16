// Rotate — spins iPrev around the frame center. Pixels rotated outside the
// unit square become transparent so the previous stage's edges don't tile
// back into view. `angle` is in turns (1.0 = a full rotation); `auto_spin`
// adds an iTime-driven rate in turns/sec for hands-free animation.
//
// With `fit` = 1 (default) the content auto-shrinks just enough that the
// rotated frame stays fully visible — no corners (and, for a wide frame, no
// horizontal edges) get cropped. `fit` = 0 keeps the old fill-and-crop look.
// MIT License.

uniform float angle;     // @range -1..1 default=0
uniform float auto_spin; // @range -2..2 default=0
uniform float fit;       // @range 0..1  default=1   shrink-to-fit (avoid cropping)

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float a = (angle + auto_spin * iTime) * 6.28318530718;
    float c = cos(a), s = sin(a);

    // Aspect-correct the rotation so a circle stays a circle on a
    // 16:9 framebuffer instead of becoming an oval.
    float aspect = iResolution.x / iResolution.y;

    // Shrink factor so the rotated frame fits inside the frame on each axis.
    // A frame of size aspect×1 rotated by `a` has a bounding box of
    // (aspect|c|+|s|) × (aspect|s|+|c|); scale it back into aspect×1.
    float ca = abs(c), sa = abs(s);
    float fx = aspect / (aspect * ca + sa);
    float fy = 1.0    / (aspect * sa + ca);
    float shrink = mix(1.0, min(fx, fy), clamp(fit, 0.0, 1.0));

    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    vec2 r = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    r /= max(shrink, 0.0001);                 // unscale → content shrinks to fit
    vec2 q = r / vec2(aspect, 1.0) + 0.5;

    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) {
        fragColor = vec4(0.0);   // transparent: don't tile, don't blank lower layers
    } else {
        fragColor = texture(iPrev, q);
    }
}
