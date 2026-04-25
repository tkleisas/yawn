// HSV — colour shift in HSV space. Rotate hue, cut/boost saturation,
// cut/boost value. Useful for fast colour-grading without touching
// individual RGB channels — `hue` lets you rainbow-shift the whole
// frame, `saturation` washes to grayscale at 0 or pushes to vivid
// neon at >1, `value` controls overall brightness.
//
// Conversion uses Sam Hocevar's branchless RGB↔HSV — the e=1e-10
// term keeps the divide stable on pure-grey pixels.
// MIT License.

uniform float hue;        // @range -1..1 default=0
uniform float saturation; // @range 0..2 default=1.0
uniform float value;      // @range 0..2 default=1.0

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)),
                d / (q.x + e),
                q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture(iPrev, uv).rgb;

    vec3 hsv = rgb2hsv(col);
    hsv.x = fract(hsv.x + hue);                      // wrap hue at 1
    hsv.y = clamp(hsv.y * saturation, 0.0, 1.0);
    hsv.z = max(hsv.z * value, 0.0);                 // brightness gain

    fragColor = vec4(hsv2rgb(hsv), 1.0);
}
