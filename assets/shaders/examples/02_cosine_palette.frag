// Cosine palette sweep — Iñigo Quilez's palette trick, with the four vec3
// knobs collapsed to playable scalar phases. Kick-driven brightness bump.
// MIT License.

uniform float speed;        // @range 0..3 default=0.5
uniform float phaseR;       // @range 0..1 default=0.00
uniform float phaseG;       // @range 0..1 default=0.33
uniform float phaseB;       // @range 0..1 default=0.67
uniform float kickPunch;    // @range 0..2 default=1.0

vec3 palette(float t, float pr, float pg, float pb) {
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(pr, pg, pb)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float t = iTime * speed + uv.x * 0.5 + uv.y * 0.3;
    vec3 col = palette(t, phaseR, phaseG, phaseB);
    col *= 1.0 + kickPunch * iKick;
    fragColor = vec4(col, 1.0);
}
