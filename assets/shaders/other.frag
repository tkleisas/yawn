// YAWN default visual shader — Shadertoy idiom.
//
// Author your own: write a `mainImage(out vec4 fragColor, in vec2 fragCoord)`
// function and you can paste in pretty much any Shadertoy fragment verbatim.
//
// Uniforms (all bound by YAWN each frame):
//   vec3  iResolution           viewport resolution in pixels (z = pixel aspect)
//   float iTime                 playback time in seconds
//   float iTimeDelta            time since last frame
//   int   iFrame                frame counter
//   vec4  iMouse                xy = current, zw = click (not yet wired)
//   sampler2D iChannel0..3      input channels (dummy 1×1 black for now)
//   vec3  iChannelResolution[4] per-channel resolution
//   float iChannelTime[4]       per-channel time
//   vec4  iDate                 (year, month, day, seconds)
//   float iSampleRate           audio sample rate
//
// YAWN-specific additions (not in Shadertoy):
//   float iBeat                 transport position in beats
//   float iTransportPlaying     1.0 if transport is playing, 0.0 otherwise

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(1.0, 3.0, 5.0));
    fragColor = vec4(col, 1.0);
}
