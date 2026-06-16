// Star Wars-style perspective text crawl.
// The clip's text (iChannel1 — right-click the clip → "Set Text…") is wrapped
// into stacked lines on a plane that recedes to a vanishing point at the
// centre of the screen and scrolls toward it. The text channel is a single
// line, so lines wrap at a fixed pixel width (no word-aware wrapping) and the
// whole message loops. Outputs alpha=0 outside the glyphs, so it can layer
// over a starfield. Use Normal blend. MIT License.

uniform float speed;     // @range 0..3      default=0.4  crawl speed
uniform float lineWidth; // @range 150..1600 default=560  text-px per wrapped line
uniform float horizon;   // @range 0.3..0.95 default=0.5  vanishing-point height (0.5 = centre)
uniform float density;   // @range 2..24     default=9    line packing toward the vanishing point
uniform float spread;    // @range 0.1..2.0  default=0.5  near-text width
uniform float r;         // @range 0..1 default=1.0
uniform float g;         // @range 0..1 default=0.86
uniform float b;         // @range 0..1 default=0.08

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;          // y: 0 bottom .. 1 top
    vec3  col   = vec3(0.0);
    float alpha = 0.0;

    float yy = horizon - uv.y;                     // > 0 below the vanishing point
    if (yy > 0.003) {
        // Perspective depth: small near the bottom, → huge at the vanishing
        // point. Referenced to the bottom edge so the indexing starts at 0.
        float depth = 1.0 / yy;
        float dMin  = 1.0 / horizon;               // depth at the bottom of the screen

        // Rows recede with depth and advance toward the vanishing point over
        // time. Lower screen rows = later (higher-index) lines.
        float lineF   = iTime * speed - (depth - dMin) * density * 0.25;
        float lineIdx = floor(lineF);
        float rowF    = fract(lineF);              // position down the line slot

        // Loop the message: how many fixed-width lines the rasterised text fills.
        float totalLines = max(floor(iTextWidth / lineWidth), 1.0);
        float wrapped    = mod(lineIdx, totalLines);

        // Horizontal foreshortening: a screen column spans a WIDER slice of the
        // line as depth grows, so near rows read large and far rows shrink.
        float lx = (uv.x - 0.5) * depth * spread;  // line-local x, 0 = centre

        float glyphFrac = 0.80;                     // glyph band; rest is line gap
        if (abs(lx) < 0.5 && rowF < glyphFrac) {
            float px = wrapped * lineWidth + (lx + 0.5) * lineWidth;
            if (px >= 0.0 && px <= iTextWidth) {
                float u = px / iTextTexWidth;
                float v = rowF / glyphFrac;        // upright glyphs (tops → vanishing point)
                float a = texture(iChannel1, vec2(u, v)).r;
                float fade = smoothstep(0.0, 0.10, yy);   // dim toward the vanishing point
                alpha = a * fade;
                col   = vec3(r, g, b) * alpha;     // premultiplied
            }
        }
    }
    fragColor = vec4(col, alpha);
}
