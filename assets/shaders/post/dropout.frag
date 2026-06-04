// Dropout — broadcast signal-loss bands. Horizontal slices of the image
// drop to static, black, or a sideways-torn copy; the kick punches a brief
// full-frame loss. Density rides iAudioLow so the corruption breathes with
// the low end. Preserves iPrev alpha so it stays layerable. MIT License.

uniform float density;  // @range 0..1  default=0.15   // base fraction that drops
uniform float reactive; // @range 0..3  default=1.0    // audio drive on density
uniform float bandSize; // @range 2..64 default=12     // band height in px

float hash11(float n) { return fract(sin(n * 91.3458) * 47453.5453); }
float hash21(vec2 p)  { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Band index + a 12Hz step so dropouts flicker like a failing feed.
    float band = floor(fragCoord.y / max(bandSize, 1.0));
    float t12  = floor(iTime * 12.0);
    float roll = hash21(vec2(band, t12));

    // Drive: base density + low-band energy + a kick-triggered global surge.
    float drive = clamp(density + reactive * iAudioLow * 0.5 + iKick * 0.4,
                        0.0, 1.0);
    float dropped = step(1.0 - drive, roll);

    vec4 col = texture(iPrev, uv);

    if (dropped > 0.5) {
        float kind = hash11(band + t12 * 0.37);
        if (kind < 0.4) {
            // Horizontal tear — shove the whole band sideways.
            float shift = (hash11(band * 3.1 + t12) - 0.5) * 0.3;
            col = texture(iPrev, vec2(fract(uv.x + shift), uv.y));
        } else if (kind < 0.75) {
            // RGB static — corrupt colour, keep the source alpha.
            col.rgb = vec3(hash21(fragCoord * 0.5 + t12),
                           hash21(fragCoord.yx + t12),
                           hash21(fragCoord * 1.7 + t12));
        } else {
            // Black-out drop.
            col.rgb = vec3(0.0);
        }
    }
    fragColor = col;
}
