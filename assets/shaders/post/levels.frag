// Levels — Photoshop-style black-point / white-point / gamma remap.
// Pulls the input range [black..white] to fill [0..1], then applies
// gamma to redistribute midtones. Black=0, white=1, gamma=1 is the
// identity. Useful for crushing blacks (raise black), blowing out
// highlights (lower white), or lifting shadows (gamma > 1).
// MIT License.

uniform float black; // @range 0..0.5 default=0
uniform float white; // @range 0.5..1 default=1
uniform float gamma; // @range 0.1..3 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c  = texture(iPrev, uv).rgb;

    // Avoid divide-by-zero if the user pinches black and white
    // together — keep at least a 0.001 gap so the remap stays sane.
    float span = max(white - black, 0.001);
    c = (c - black) / span;
    c = clamp(c, 0.0, 1.0);
    c = pow(c, vec3(1.0 / max(gamma, 0.001)));

    fragColor = vec4(c, 1.0);
}
