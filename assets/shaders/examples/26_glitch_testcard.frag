// Glitch test card — SMPTE-style colour bars that tear, block-drop, and
// resync on the beat, with a kick-punched signal-loss banner. A
// self-contained "broadcast malfunction" backdrop; use as a track's
// source shader. Overlay a text layer (e.g. "NO SIGNAL") on top for the
// full effect. MIT License.

uniform float glitch; // @range 0..1  default=0.6   // tear / block-drop intensity
uniform float scan;   // @range 0..1  default=0.3   // scanline darkening

float hash11(float n) { return fract(sin(n * 78.233) * 43758.5453); }
float hash21(vec2 p)  { return fract(sin(dot(p, vec2(12.99, 78.23))) * 43758.5453); }

vec3 smpte(float x) {
    // Seven standard-ish bars, white → blue, left to right.
    int i = int(clamp(floor(x * 7.0), 0.0, 6.0));
    if (i == 0) return vec3(0.75);
    if (i == 1) return vec3(0.75, 0.75, 0.0);
    if (i == 2) return vec3(0.0,  0.75, 0.75);
    if (i == 3) return vec3(0.0,  0.75, 0.0);
    if (i == 4) return vec3(0.75, 0.0,  0.75);
    if (i == 5) return vec3(0.75, 0.0,  0.0);
    return vec3(0.0, 0.0, 0.75);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Beat-synced surge — pops right after each beat then decays, plus a
    // kick boost. This is the "low floor + spike" reactivity.
    float beatPhase = fract(iBeat);
    float surge = glitch * (0.2 + 0.8 * exp(-beatPhase * 6.0));
    surge = clamp(surge + iKick * 0.5, 0.0, 1.0);

    // Horizontal block tear — shove some bands sideways, refreshed fast.
    float band = floor(uv.y * 24.0);
    float t    = floor(iTime * 20.0);
    float tear = (hash21(vec2(band, t)) - 0.5) * surge * 0.5
                 * step(0.6, hash21(vec2(band, t + 3.0)));
    float x = fract(uv.x + tear);

    // Bars on the top 80%, a data strip on the bottom 20%.
    vec3 col;
    if (uv.y > 0.2) {
        col = smpte(x);
    } else {
        float seg = floor(x * 6.0);
        col = vec3(mod(seg, 2.0) * 0.2 + 0.05);     // PLUGE-ish grey blocks
        if (x > 0.66) {                             // a moving noise field
            float n = hash21(vec2(floor(x * 80.0), floor(uv.y * 30.0) + t));
            col = vec3(n);
        }
    }

    // Block-drop — random blocks blank to static or black during a surge.
    vec2 blk = floor(vec2(x, uv.y) * vec2(16.0, 12.0));
    if (step(1.0 - surge * 0.5, hash21(blk + t * 1.3)) > 0.5) {
        float n = hash21(blk + t);
        col = (n > 0.5) ? vec3(n) : vec3(0.0);
    }

    // Kick-punched red signal-loss banner across the middle.
    float banner = step(abs(uv.y - 0.5), 0.06) * iKick;
    col = mix(col, vec3(0.9, 0.05, 0.05), banner * 0.85);

    // Scanlines.
    col *= 1.0 - scan * 0.5 * step(1.0, mod(fragCoord.y, 2.0));

    fragColor = vec4(col, 1.0);
}
