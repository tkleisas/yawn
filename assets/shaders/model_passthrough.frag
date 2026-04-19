// Passthrough for 3D model clips. The model render lands in iChannel2
// already lit and framed — we just blit it. The @range uniforms below
// don't appear in any GLSL math here: the C++ engine reads them by
// name each frame and feeds them into M3DRenderer's transform. Map
// A..H knobs to them (or hook up LFOs) to animate the model live.

uniform float modelPosX;   // @range -5..5 default=0
uniform float modelPosY;   // @range -5..5 default=0
uniform float modelPosZ;   // @range -10..10 default=0
uniform float modelRotX;   // @range 0..360 default=0
uniform float modelRotY;   // @range 0..360 default=0
uniform float modelRotZ;   // @range 0..360 default=0
// Continuous-rotation speeds in degrees per second. Integrated by the
// engine over wall-clock time and added onto the static modelRot*
// values above. Set to 0 for static pose; set modelSpinY = 45 for a
// slow turntable spin.
uniform float modelSpinX;  // @range -360..360 default=0
uniform float modelSpinY;  // @range -360..360 default=0
uniform float modelSpinZ;  // @range -360..360 default=0
uniform float modelScale;  // @range 0.1..10 default=1

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = texture(iChannel2, uv);
}
