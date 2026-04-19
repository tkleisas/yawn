// YAWN default visual shader — Shadertoy idiom.
//
// Author your own: write a `mainImage(out vec4 fragColor, in vec2 fragCoord)`
// function and you can paste in pretty much any Shadertoy fragment verbatim.
//
// Uniforms (all bound by YAWN each frame):
//   vec3  iResolution           viewport resolution in pixels (z = pixel aspect)
//   float iTime                 wall-clock seconds since shader load
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
//   float iTransportTime        transport position in seconds
//   float iTransportPlaying     1.0 if transport is playing, 0.0 otherwise
//   float iAudioLevel           0..1 envelope on the clip's audio source
//   float iAudioLow/Mid/High    0..1 envelope per band (LP 200 / BP 800 / HP 2k)
//   float iKick                 0..1 decaying impulse on low-band onsets
//   float knobA..knobH          0..1 generic controllable knobs (always bound)

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Generic knobs — remapped for meaningful local use:
    //   A → palette speed        B → palette hue offset
    //   C → audio-splash strength D → kick-flash strength
    //   E → spectrum bar height   F..H reserved for you
    float pSpeed   = 0.5 + knobA * 3.0;
    float hueOff   = knobB;
    float audioAmt = knobC * 2.0;
    float kickAmt  = knobD * 0.6;
    float specH    = knobE * 0.3;

    // Cosine palette, tweakable.
    vec3 col = 0.5 + 0.5 * cos(iTime * pSpeed + uv.xyx
                                 + vec3(0.0, 2.0, 4.0) + hueOff * 6.28318);

    float pulse = pow(1.0 - fract(iBeat), 4.0) * iTransportPlaying;
    col += pulse * 0.25;

    vec2 centered = uv - 0.5;
    float r = length(centered);
    float fall = exp(-r * 4.0);
    col += fall * vec3(iAudioLow, iAudioMid, iAudioHigh) * audioAmt;

    col += iKick * vec3(1.0, 0.85, 0.6) * kickAmt;

    if (uv.y < specH) {
        float f   = uv.x;
        float mag = texture(iChannel0, vec2(f, 0.25)).x;
        float h   = mag * specH;
        float inBar = step(uv.y, h);
        col = mix(col, vec3(1.0, 1.0, 1.0), inBar * 0.35);
    }

    fragColor = vec4(col, 1.0);
}
