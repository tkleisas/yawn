// 43 · Cymatics — a Chladni plate: fine sand settles on the nodal lines
// of a standing-wave figure
//     cos(nπx)cos(mπy) − cos(mπx)cos(nπy).
// The mode numbers snap with the audio bands (low drives n, high drives
// m), so the figure re-draws itself as the music moves; kicks flash a
// secondary mode over the top. Gold sand on a dark plate. Deterministic
// per time + knobs. MIT License.

uniform float modeN;   // @range 2..9  default=3  base mode n
uniform float modeM;   // @range 2..9  default=5  base mode m
uniform float spread;  // @range 0..1  default=0.35 nodal-line width
uniform float grain;   // @range 0..1  default=0.75 sand grain
uniform float kick;    // @range 0..1  default=0.60
uniform float palette; // @range 0..1  default=0.45
uniform float seed;    // @range 0..1  default=0.0

const float PI = 3.141592653589793;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Chladni figure value in plate coords (x,y in -1..1).
float chladni(vec2 p, float n, float m) {
    return cos(n * PI * p.x) * cos(m * PI * p.y)
         - cos(m * PI * p.x) * cos(n * PI * p.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 p = (fragCoord - 0.5 * res) / res.y;
    float r = length(p);

    // ── seed probes: palette (family convention) ──
    vec3 s0 = texture(iChannel2, vec2(0.50, 0.50)).rgb;
    vec3 s2 = texture(iChannel2, vec2(0.82, 0.28)).rgb;
    vec3 s3 = texture(iChannel2, vec2(0.30, 0.82)).rgb;
    vec3 avg = (s0 + s2 + s3) / 3.0;
    float lum = dot(avg, vec3(0.299, 0.587, 0.114));

    vec3 plate = mix(avg * 0.10 + vec3(0.010, 0.012, 0.020),
                     vec3(0.015, 0.018, 0.035), palette);
    vec3 sand = mix(clamp(avg * vec3(1.5, 1.2, 0.7) + 0.20, 0.0, 1.0),
                    vec3(0.90, 0.74, 0.38), palette);

    // ── mode selection (snaps with the bands; never below 2) ──
    float n = clamp(floor(modeN + 1.0 + iAudioLow * 4.0), 2.0, 11.0);
    float m = clamp(floor(modeM + 1.0 + iAudioHigh * 4.0), 2.0, 11.0);
    // Keep n != m (that figure degenerates to zero).
    if (abs(n - m) < 0.5) m = min(m + 1.0, 11.0);

    vec2 q = p / 0.46;                       // plate radius ~0.46
    float outside = smoothstep(1.0, 0.985, length(q));

    float v = chladni(q, n, m);
    float w = 0.02 + 0.16 * spread;
    float nodes = 1.0 - smoothstep(0.0, w, abs(v));

    // Sand: grain breaks the lines up; brighten with the bands.
    float g = mix(1.0, 0.55 + 0.45 * hash21(floor(fragCoord * 1.4)),
                  grain);
    float line = nodes * g * outside;
    line *= 0.55 + 0.45 * smoothstep(0.0, 0.6, lum);

    vec3 col = plate;
    // faint radial sheen on the plate
    col += mix(plate, sand, 0.25) * exp(-r * 4.0) * 0.25;
    col += sand * line * (0.65 + 0.5 * clamp(iKick, 0.0, 1.0) * kick);

    // Kick flash: a secondary, higher mode superimposed briefly.
    float k = clamp(iKick, 0.0, 1.0) * kick;
    if (k > 0.02) {
        float v2 = chladni(q, min(m + 2.0, 13.0), min(n + 3.0, 13.0));
        float line2 = (1.0 - smoothstep(0.0, w * 0.6, abs(v2))) * outside;
        col += mix(sand, vec3(1.0), 0.35) * line2 * k * 0.55;
    }

    // Plate rim + vignette.
    col += sand * (1.0 - smoothstep(0.0025, 0.006, abs(length(q) - 1.0)))
         * 0.35;
    col *= 1.0 - smoothstep(0.62, 1.05, length(p)) * 0.55;
    col += (hash21(fragCoord + seed * 3.0) - 0.5) * 0.012;

    fragColor = vec4(col, 1.0);
}
