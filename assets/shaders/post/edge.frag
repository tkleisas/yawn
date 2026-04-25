// Edge — Sobel edge detection on iPrev's luminance. Outputs the edge
// magnitude as grayscale (white edges on black background); chain
// after bloom for glowing-line effects, or before invert for black
// lines on a coloured background.
// MIT License.

uniform float intensity; // @range 0..4 default=1.5
uniform float threshold; // @range 0..1 default=0.0

float lum(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy;

    float tl = lum(texture(iPrev, uv + vec2(-px.x,  px.y)).rgb);
    float t  = lum(texture(iPrev, uv + vec2( 0.0,   px.y)).rgb);
    float tr = lum(texture(iPrev, uv + vec2( px.x,  px.y)).rgb);
    float l  = lum(texture(iPrev, uv + vec2(-px.x,  0.0 )).rgb);
    float r  = lum(texture(iPrev, uv + vec2( px.x,  0.0 )).rgb);
    float bl = lum(texture(iPrev, uv + vec2(-px.x, -px.y)).rgb);
    float b  = lum(texture(iPrev, uv + vec2( 0.0,  -px.y)).rgb);
    float br = lum(texture(iPrev, uv + vec2( px.x, -px.y)).rgb);

    // Sobel kernels:
    //  Gx = [-1 0 1; -2 0 2; -1 0 1]
    //  Gy = [-1 -2 -1; 0 0 0; 1 2 1]
    float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
    float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
    float g  = sqrt(gx*gx + gy*gy) * intensity;
    if (g < threshold) g = 0.0;
    g = clamp(g, 0.0, 1.0);

    fragColor = vec4(vec3(g), 1.0);
}
