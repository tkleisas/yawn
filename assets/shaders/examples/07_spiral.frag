// Rotating log-spiral, speed + arm count controlled.
// MIT License.

uniform float arms;       // @range 1..12 default=4
uniform float speed;      // @range -3..3 default=0.8
uniform float tightness;  // @range 0.1..3.0 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    float phase = a * arms + log(max(r, 0.001)) * tightness * 6.0 + iTime * speed;
    float v = 0.5 + 0.5 * sin(phase);
    v *= smoothstep(0.8, 0.0, r);

    vec3 col = 0.5 + 0.5 * cos(6.28318 * (v + vec3(0.1, 0.4, 0.7)));
    col *= v;
    fragColor = vec4(col, 1.0);
}
