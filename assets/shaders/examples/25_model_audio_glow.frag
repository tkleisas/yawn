// 3D model with audio-reactive glow + band tint.
//
// Drops into any clip that has a .glb assigned via
// right-click → "Set Model…". The engine renders the model into
// iChannel2 already lit and framed; this shader post-processes that
// image with:
//   • A kick-triggered radial bloom around bright pixels
//   • A low/mid/high tint that nudges the model's palette
//
// modelSpinY defaults to 45 °/s so out-of-the-box the model does a
// slow turntable spin — set it to 0 if you want a static pose and
// drive rotation from knobs instead.
//
// MIT License.

uniform float modelPosX;   // @range -5..5 default=0
uniform float modelPosY;   // @range -5..5 default=0
uniform float modelPosZ;   // @range -10..10 default=0
uniform float modelRotX;   // @range 0..360 default=0
uniform float modelRotY;   // @range 0..360 default=0
uniform float modelRotZ;   // @range 0..360 default=0
uniform float modelSpinX;  // @range -360..360 default=0
uniform float modelSpinY;  // @range -360..360 default=45
uniform float modelSpinZ;  // @range -360..360 default=0
uniform float modelScale;  // @range 0.1..10 default=1

uniform float glowAmount;  // @range 0..2 default=1.0
uniform float tintAmount;  // @range 0..1 default=0.35

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec4 src = texture(iChannel2, uv);

    // Kick-driven bloom: iKick is already peak-triggered and decays
    // over ~100 ms, so we use it directly as the glow intensity and
    // a small radial blur radius. Bright areas contribute the most.
    float glow = iKick * glowAmount;
    vec3 bloom = vec3(0.0);
    if (glow > 0.01) {
        const int kSteps = 6;
        float radius = 0.010 * glow;
        for (int i = 0; i < kSteps; ++i) {
            float a = float(i) * (6.2831853 / float(kSteps));
            vec2 off = vec2(cos(a), sin(a)) * radius;
            vec4 s = texture(iChannel2, uv + off);
            float br = max(max(s.r, s.g), s.b);
            bloom += s.rgb * max(br - 0.45, 0.0);
        }
        bloom /= float(kSteps);
    }

    // Band-driven tint: low→red, mid→green, high→blue. tintAmount=0
    // keeps the original colour; higher values push harder.
    vec3 bandTint = 1.0 + vec3(iAudioLow, iAudioMid, iAudioHigh) * 0.9;
    vec3 tinted = src.rgb * mix(vec3(1.0), bandTint, tintAmount);

    fragColor = vec4(tinted + bloom * glow, src.a);
}
