// Strobe locked to beat divisions — great as a top Add-blend layer.
// MIT License.

uniform float division;  // @range 1..8 default=4    — strobes per bar (4/4)
uniform float duty;      // @range 0..0.5 default=0.1
uniform float r;         // @range 0..1 default=1.0
uniform float g;         // @range 0..1 default=1.0
uniform float b;         // @range 0..1 default=1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float phase = fract(iBeat * division / 4.0);  // 0..1 per flash cycle
    float on = step(phase, duty) * iTransportPlaying;
    vec3 col = vec3(r, g, b) * on;
    fragColor = vec4(col, 1.0);
}
