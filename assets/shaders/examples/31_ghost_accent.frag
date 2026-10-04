// Live-coding ghost accents — reacts to Y.A.W.N's improv layer: each
// pending note fire is a rising glyph on its bar position; fired pops
// come through iChannel0's note bus via texture (iAudioLevel rides the
// mix). MIT License.
//
// uniforms (Y.A.W.N-specific, all optional):
//   iBeat         absolute transport beats (built-in)
//   iBeatBarFrac  0..1 position inside the current bar (built-in)
//   iGhostCount   number of upcoming improv fires (0..8)
//   iGhostN       vec4(pitch01, vel01, beatsUntilFire, trackF)
uniform float ring;    // @range 0.5..2 default=1.0
uniform float spin;    // @range 0..4 default=1.2

vec3 tone(float t) {
    // cyan → violet ramp keyed by fire immediacy
    return clamp(vec3(0.15 + 0.85 * t, 0.35 + 0.4 * t, 1.2 - 0.55 * t),
                 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (uv - vec2(0.5, 0.42)) * vec2(iResolution.x / iResolution.y, 1.0);

    float acc = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i > int(iGhostCount)) break;
        // iGhostN: (pitch01, vel01, beatsUntilFire, track)
        vec4 g = (i == 0) ? iGhost0 : (i == 1) ? iGhost1 : (i == 2) ? iGhost2
              : (i == 3) ? iGhost3 : (i == 4) ? iGhost4 : (i == 5) ? iGhost5
              : (i == 6) ? iGhost6 : iGhost7;
        const float dt = 0.016;   // frames-per-beat approx for the pull-in
        float t01 = clamp(1.0 - g.z / max(g.z + 0.25, 0.25), 0.0, 1.0);

        // The glyph orbits clockwise toward the fire moment; angular
        // position = its scheduled bar phase (.untilFire scaled).
        float angle = g.w * 1.5708 + spin * iBeatBarFrac * 6.2832
                    + t01 * 2.2;
        vec2 dir = vec2(cos(angle), sin(angle));
        float orbit = ring * (0.38 - 0.22 * t01);
        vec2 c = dir * orbit;
        float h = 0.03 + 0.07 * g.y;
        float d = length((p - c) / vec2(1.0, h / h + 0.9));
        float glyph = smoothstep(0.5, 0.1, d);
        acc += glyph * tone(t01).g * g.y;
    }

    // responding halo from whatever just fired (via note bus texture —
    // use the master level as the "afterglow" proxy)
    float glowChip = pow(clamp(iAudioLevel * 2.4, 0.0, 1.0), 2.0);
    float halo = smoothstep(1.05 * ring, 0.15, length(p)) * glowChip * 0.6;
    acc += halo;

    vec3 col = tone(0.35) * 0.12 + tone(clamp(acc, 0.0, 1.0)) * min(acc, 1.6);
    col += vec3(0.10, 0.16, 0.24) * smoothstep(1.4, 0.0, length(p));
    fragColor = vec4(col, 1.0);
}
