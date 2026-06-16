// 60's-style rotating hypnotic spiral.
// A two-tone logarithmic swirl with an adjustable arm count, two hue-picked
// colours, an orbiting centre, and optionally wavy arms. MIT License.

uniform float arms;         // @range 2..16  default=6     number of spiral arms
uniform float color1;       // @range 0..1   default=0.95  hue of tone A
uniform float color2;       // @range 0..1   default=0.58  hue of tone B
uniform float centerRadius; // @range 0..0.5 default=0.0   centre orbits a circle of this radius
uniform float waviness;     // @range 0..1   default=0.0   sine-warp of the arms
uniform float speed;        // @range -20..20 default=0.8  rotation speed

// Hue (0..1) → full-saturation, full-value RGB.
vec3 hue2rgb(float h) {
    vec3 k = abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0;
    return clamp(k, 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Aspect-correct, screen-centred coordinates (~[-0.9..0.9] in x).
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // The spiral centre drifts slowly around a circle of radius centerRadius
    // (0 = pinned to screen centre) for a wandering, hypnotic feel.
    float ct = iTime * 0.3;
    vec2 center = centerRadius * vec2(cos(ct), sin(ct));
    vec2 p = uv - center;

    float r = length(p);
    float a = atan(p.y, p.x);

    // Round to an integer arm count so the arms wrap seamlessly at ±pi.
    float n = floor(arms + 0.5);

    // Wavy arms: a radial sine perturbation, scaled by waviness.
    float wave = waviness * 0.6 * sin(r * 16.0 - iTime * speed * 2.0);

    // Spiral phase = arms·angle + logarithmic twist + rotation + wave.
    float phase = a * n + log(max(r, 0.0015)) * 6.0 + iTime * speed + wave;

    // Sharpen the sine into a clean two-tone band between the colours.
    float band = smoothstep(0.4, 0.6, 0.5 + 0.5 * sin(phase));
    vec3  col  = mix(hue2rgb(color1), hue2rgb(color2), band);

    // Soft disc vignette so the corners fall to black.
    col *= 1.0 - smoothstep(0.9, 1.3, r);

    fragColor = vec4(col, 1.0);
}
