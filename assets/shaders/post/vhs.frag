// VHS — videotape-decay glitch. Combines the four most recognisable
// artefacts: per-scanline horizontal jitter, chromatic aberration
// (R/B offset), occasional tracking-error band tears, and scanline
// darkening. `intensity` is the master mix; the other knobs scale
// each artefact independently so you can dial in the exact look.
// All animation is iTime-driven so it lives without external macros.
// MIT License.

uniform float intensity; // @range 0..1 default=0.5
uniform float chroma;    // @range 0..1 default=0.4
uniform float jitter;    // @range 0..1 default=0.3
uniform float tear;      // @range 0..1 default=0.4

float hash(float n) {
    return fract(sin(n * 12.9898) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Per-scanline horizontal jitter — every scanline picks its own
    // tiny offset, refreshed at 30Hz so the noise reads as VHS chatter
    // rather than a continuous wobble.
    float scan = floor(uv.y * iResolution.y);
    float t30  = floor(iTime * 30.0);
    float jit  = (hash(scan + t30 * 0.137) - 0.5) * jitter * intensity * 0.04;

    // Tracking-error tear — once every few seconds, an 8-band
    // segment of the frame slips sideways like a worn head gap.
    float tearGate  = step(0.92, fract(iTime * 0.8));   // ~8% duty
    float bandY     = floor(uv.y * 8.0);
    float tearShift = tearGate * (hash(bandY + floor(iTime * 0.8)) - 0.5)
                       * tear * intensity * 0.15;

    vec2 sampleUV = vec2(fract(uv.x + jit + tearShift), uv.y);

    // Chromatic aberration — split RGB across a few pixels.
    float ch = chroma * intensity * 0.006;
    float r = texture(iPrev, sampleUV - vec2(ch, 0.0)).r;
    float g = texture(iPrev, sampleUV).g;
    float b = texture(iPrev, sampleUV + vec2(ch, 0.0)).b;
    vec3 c = vec3(r, g, b);

    // Scanline darkening — alternate rows attenuated. mix() against
    // intensity so dialling intensity to 0 cleanly returns the
    // unmodified image instead of a still-darkened version.
    float dim = 1.0 - 0.15 * intensity * mod(scan, 2.0);
    c *= dim;

    fragColor = vec4(c, 1.0);
}
