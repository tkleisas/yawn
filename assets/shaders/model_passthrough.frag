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

// Camera. Like the model* uniforms above, these are read by the C++
// engine (not used in GLSL here) and fed into M3DRenderer's camera.
// fov = 0 means "auto-frame the model" (the historical behaviour); set
// fov > 0 to switch to a free camera positioned by cameraPos*/cameraTarget*.
// A scene script's `camera = {...}` return overrides these for that frame.
uniform float fov;           // @range 0..120 default=0
uniform float cameraPosX;    // @range -10..10 default=0
uniform float cameraPosY;    // @range -10..10 default=0
uniform float cameraPosZ;    // @range 0.5..20 default=3
uniform float cameraTargetX; // @range -5..5 default=0
uniform float cameraTargetY; // @range -5..5 default=0
uniform float cameraTargetZ; // @range -5..5 default=0

// Lighting (also read by the engine, fed to the model's lit shader).
// yaw/pitch place the directional light; ambient is fill; intensity
// scales the key light. Map A..H knobs / LFOs for live light moves.
uniform float lightYaw;       // @range -180..180 default=45
uniform float lightPitch;     // @range 0..90 default=45
uniform float lightAmbient;   // @range 0..1 default=0.2
uniform float lightIntensity; // @range 0..3 default=1

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = texture(iChannel2, uv);
}
