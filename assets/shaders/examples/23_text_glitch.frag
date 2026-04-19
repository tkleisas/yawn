// Glitched text with RGB split + horizontal tearing. Audio-reactive —
// glitch intensity rises with iAudioHigh (hats/cymbals). Transparent
// outside the glyph pixels, so stack on top of another layer freely.
// MIT License.

uniform float glitch;  // @range 0..1 default=0.3
uniform float tear;    // @range 0..0.5 default=0.08
uniform float size;    // @range 0.3..2 default=1.0

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    float stripH     = 0.40 * size;
    float bandCenter = 0.5;
    float bandHalf   = stripH * 0.5;
    float inBand     = step(abs(uv.y - bandCenter), bandHalf);

    float fill       = iTextWidth / iTextTexWidth;
    float texAspect  = iTextWidth / iChannelResolution[1].y;
    float textScreenW = stripH * texAspect * (iResolution.y / iResolution.x);
    float u0 = (uv.x - 0.5) / textScreenW + 0.5;
    float u  = u0 * fill;
    float v  = 1.0 - ((uv.y - (bandCenter - bandHalf)) / stripH);

    float g = glitch * (0.3 + iAudioHigh * 1.5);
    float tearShift = (hash(vec2(floor(uv.y * 200.0), floor(iTime * 12.0))) - 0.5)
                       * tear * g;
    float chroma = 0.006 * g;

    float aR = texture(iChannel1, vec2(u + tearShift - chroma, v)).r;
    float aG = texture(iChannel1, vec2(u + tearShift,           v)).r;
    float aB = texture(iChannel1, vec2(u + tearShift + chroma, v)).r;

    float inText = step(0.0, u) * step(u, fill)
                  * step(0.0, v) * step(v, 1.0) * inBand;

    float alpha = max(max(aR, aG), aB) * inText;
    fragColor = vec4(vec3(aR, aG, aB) * inText, alpha);
}
