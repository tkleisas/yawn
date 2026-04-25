// Rotate — spins iPrev around the frame center. Pixels rotated outside
// the unit square clamp to black so the previous stage's edges don't
// tile back into view. `angle` is in turns (1.0 = a full rotation);
// `auto_spin` adds an iTime-driven rate in turns/sec for hands-free
// animation when the macro device isn't wired up yet.
// MIT License.

uniform float angle;     // @range -1..1 default=0
uniform float auto_spin; // @range -2..2 default=0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float a = (angle + auto_spin * iTime) * 6.28318530718;
    float c = cos(a), s = sin(a);

    // Aspect-correct the rotation so a circle stays a circle on a
    // 16:9 framebuffer instead of becoming an oval.
    float aspect = iResolution.x / iResolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    vec2 r = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    vec2 q = r / vec2(aspect, 1.0) + 0.5;

    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        fragColor = texture(iPrev, q);
    }
}
