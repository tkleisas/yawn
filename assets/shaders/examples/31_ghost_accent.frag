// Live-coding ghost accents — the visual face of Y.A.W.N's improv layer.
// Always-alive ambient (beat-locked rings + bar sweep) with one glyph
// orbit per UPCOMING improv fire, brightening as the fire moment
// approaches. MIT License.
//
// YAWN built-in uniforms used (all optional):
//   iBeat, iBeatBarFrac          — transport clocks
//   iGhostCount, iGhost0..7      — vec4(pitch01, vel01, beatsUntilFire,
//                                      trackF), in schedule order
uniform float ring;    // @range 0.2..2 default=0.85
uniform float spin;    // @range 0..4 default=0.9
uniform float ambience; // @range 0..1 default=0.34 — ambient intensity

vec3 cool(float t) {     // cyan → violet ramp
    return clamp(vec3(0.15 + 0.85 * t, 0.35 + 0.4 * t, 1.25 - 0.6 * t),
                 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (uv - vec2(0.5, 0.44)) * vec2(iResolution.x / iResolution.y, 1.0);
    float dist = length(p);

    // ── Ambient: two beat-locked rings + bar sweep + center core ──
    float barBeat = iBeatBarFrac * 4.0;                  // 0..4
    float ringPulse = 0.0;
    ringPulse += 0.5 * exp(-6.0 * abs(dist - (ring * (0.30 + 0.06 * sin(iBeat * 1.570796)))));
    ringPulse += 0.3 * exp(-5.0 * abs(dist - ring * 0.55));
    // radial spoke sweep, one rotation per bar
    float ang = atan(p.y, p.x);
    float sweep = 0.10 * exp(-8.0 * abs(fract((ang + iBeatBarFrac * 6.2831853) / 6.2831853) - 0.5) * 6.0);
    float core = 0.16 * exp(-3.0 * dist);
    float amb = (ringPulse + sweep + core) * max(ambience, 0.0);

    vec3 col = vec3(0.06, 0.10, 0.16) + cool(0.30) * amb;

    // ── Upcoming improv fires: orbiting glyphs brightening at fire ──
    for (int i = 0; i < 8; ++i) {
        if (i >= int(iGhostCount)) break;
        vec4 g = (i == 0) ? iGhost0 : (i == 1) ? iGhost1 : (i == 2) ? iGhost2
              : (i == 3) ? iGhost3 : (i == 4) ? iGhost4 : (i == 5) ? iGhost5
              : (i == 6) ? iGhost6 : iGhost7;
        float t01 = 1.0 - clamp(g.z / 4.0, 0.0, 1.0);   // 0 = far, 1 = firing
        float angle = g.w * 1.5708 + spin * iBeatBarFrac * 6.2831853;
        vec2 dir = vec2(cos(angle), sin(angle));
        vec2 c = dir * (ring * (0.62 - 0.34 * t01));
        float h = 0.045 + 0.075 * g.y;
        float d = length((p - c) * vec2(1.0, (0.03 + h) / h));
        float glyph = smoothstep(0.55, 0.05, d);
        col += vec3(0.35, 0.65, 1.0) * glyph * g.y * (0.25 + 1.15 * t01);
        // trailing wake behind near-fire glyphs
        col += cool(t01) * 0.5 * exp(-3.5 * abs(dist - length(c))) * g.y * t01;
    }

    fragColor = vec4(col, 1.0);
}
