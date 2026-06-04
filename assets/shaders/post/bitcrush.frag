// Bit-crush — quantises colour to a few bits per channel and corrupts
// blocks of the framebuffer with XOR-style channel flips, like failing
// video memory. The kick ramps the corruption. Preserves iPrev alpha so it
// stays layerable. MIT License.

uniform float bits;       // @range 1..8  default=5     // bits per channel
uniform float blockSize;  // @range 1..64 default=8     // corruption block px
uniform float corruption; // @range 0..1  default=0.15  // base corruption floor
uniform float reactive;   // @range 0..3  default=1.0   // kick drive on corruption

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv  = fragCoord / iResolution.xy;
    vec4 col = texture(iPrev, uv);

    // Quantise each channel to `bits` discrete levels.
    float levels = max(exp2(floor(bits)) - 1.0, 1.0);
    vec3 q = floor(col.rgb * levels + 0.5) / levels;

    // Per-block corruption gate, refreshed ~15Hz; floor + kick spike.
    vec2  block = floor(fragCoord / max(blockSize, 1.0));
    float t15   = floor(iTime * 15.0);
    float amt   = clamp(corruption + reactive * iKick * 0.7, 0.0, 1.0);
    float gate  = step(1.0 - amt, hash21(block + t15 * 2.3));

    // Fake bit-flip — XOR-ish per-channel scramble, re-quantised.
    vec3 flipped = abs(q - vec3(hash21(block + 7.0),
                                hash21(block + 19.0),
                                hash21(block + 31.0)));
    flipped = floor(flipped * levels + 0.5) / levels;

    fragColor = vec4(mix(q, flipped, gate), col.a);
}
