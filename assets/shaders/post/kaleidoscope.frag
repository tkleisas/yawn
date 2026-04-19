// Kaleidoscope — N-fold radial mirror of the source image.
// MIT License.

uniform float segments;  // @range 2..16 default=6.0
uniform float rotate;    // @range -3..3 default=0.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = uv - 0.5;
    // Preserve aspect so segments look geometric, not stretched.
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    p.x *= aspect;

    float r = length(p);
    float a = atan(p.y, p.x) + rotate;
    float seg = 3.14159265 / segments;
    a = mod(a, 2.0 * seg);
    a = abs(a - seg);

    vec2 sp = vec2(cos(a), sin(a)) * r;
    sp.x /= aspect;
    vec2 sampleUV = sp + 0.5;
    // Keep sampling inside the image — reflect at edges.
    sampleUV = abs(sampleUV);
    if (sampleUV.x > 1.0) sampleUV.x = 2.0 - sampleUV.x;
    if (sampleUV.y > 1.0) sampleUV.y = 2.0 - sampleUV.y;

    fragColor = texture(iPrev, sampleUV);
}
