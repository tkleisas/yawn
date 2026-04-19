// Default shader for visual clips that reference a video file. Samples
// the decoded video frame from iChannel2 and writes it full-frame.
//
// Assign a different shader to the clip if you want to manipulate the
// video (kaleidoscope, chromakey, time-echo, etc.) — your shader just
// reads iChannel2 instead of drawing something procedural.
// MIT License.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    // Flip Y so upright video appears upright on screen (compositor FBOs
    // use standard GL origin at bottom-left; our video frame was uploaded
    // with row 0 at the top).
    fragColor = texture(iChannel2, vec2(uv.x, 1.0 - uv.y));
}
