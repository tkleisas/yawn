
in vec2 vUV;
out vec4 fragColor_out;

uniform vec3      iResolution;
uniform float     iTime;
uniform float     iTimeDelta;
uniform int       iFrame;
uniform vec4      iMouse;
uniform vec4      iDate;
uniform float     iSampleRate;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform vec3      iChannelResolution[4];
uniform float     iChannelTime[4];

// Previous-pass output. On pass 0 this binds to a dummy black texture
// (no upstream pass exists); on each chain pass it binds to the prior
// pass's framebuffer colour attachment. Effect shaders sample it via
// `texture(iPrev, uv)` to compose on top of the running image.
uniform sampler2D iPrev;

// Previous *frame*'s final chain output. Lets feedback / echo / trail
// shaders sample what they emitted last frame and superimpose it on
// the current iPrev. The engine only allocates + populates this
// texture when at least one pass actually declares it (lazy), so
// layers that don't use iFeedback pay nothing. Black on the very
// first frame after the layer is created.
uniform sampler2D iFeedback;

// YAWN-specific extensions — not in Shadertoy.
// iTime advances with wall-clock; iTransportTime follows the transport
// position (stops when playback is paused). iBeat is transport beats.
// iAudioLevel is a 0..1 envelope-smoothed peak of the clip's audio source.
uniform float iBeat;
uniform float iTransportPlaying;
uniform float iTransportTime;
uniform float iAudioLevel;
uniform float iAudioLow;
uniform float iAudioMid;
uniform float iAudioHigh;
uniform float iKick;

// Live-coding improvisation ghosts (phase B): iGhostN = upcoming fires
// in schedule order — vec4(pitch/127, velocity 0..1, beats until the
// fire moment, track index). iGhostCount carries the valid entry count
// (0..8). iBeatBarFrac is the 0..1 position inside the current bar.
uniform float iGhostCount;
uniform vec4 iGhost0;
uniform vec4 iGhost1;
uniform vec4 iGhost2;
uniform vec4 iGhost3;
uniform vec4 iGhost4;
uniform vec4 iGhost5;
uniform vec4 iGhost6;
uniform vec4 iGhost7;
uniform float iBeatBarFrac;

// Rendered pixel width of the clip's text strip on iChannel1. Use this
// for wrap-correct scrolling: e.g. `mod(pxX, iTextWidth) / iTextTexWidth`.
uniform float iTextWidth;
uniform float iTextTexWidth;   // always 2048 — the texture's own width

// 8 generic knobs always bound. Pattern: lay a hardware encoder bank on
// these and every shader gets the same eight controls. Shaders that
// prefer named/annotated custom uniforms can still declare those — both
// live happily side-by-side.
uniform float knobA;
uniform float knobB;
uniform float knobC;
uniform float knobD;
uniform float knobE;
uniform float knobF;
uniform float knobG;
uniform float knobH;

void mainImage(out vec4 fragColor, in vec2 fragCoord);

void main() {
    vec4 c;
    mainImage(c, vUV * iResolution.xy);
    fragColor_out = c;
}

#line 1

// Live-coding ghost accents — the visual face of Y.A.W.N's improv layer.
// Crisp beat-locked clock face with one glowing diamond per UPCOMING
// improv fire; glyphs brighten and rush clockwise toward their fire
// moment. MIT License.
//
// YAWN built-in uniforms used (all optional):
//   iBeat, iBeatBarFrac            — transport clocks
//   iGhostCount, iGhost0..iGhost7  — vec4(pitch01, vel01,
//                                       beatsUntilFire, trackF)

uniform float spin;      // @range 0..4 default=0.5
uniform float ambience;  // @range 0..1 default=0.55

const float TAU = 6.28318530718;

float aa(float edge, float d) {       // cheap analytic AA
    return smoothstep(edge, edge - 2.0 * 0.003, d);
}

// An anti-aliased diamond mask.
float diamond(vec2 p, float r) {
    float d = abs(p.x) + abs(p.y);
    return aa(r, d);
}

mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }

vec3 cool(float t) {
    return clamp(vec3(0.12 + 0.95 * t, 0.35 + 0.45 * t, 1.30 - 0.75 * t),
                 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (uv - vec2(0.5, 0.5)) * vec2(iResolution.x / iResolution.y, 1.0);
    float dist = length(p);

    // ── Backdrop: vertical blues + vignette ──
    vec3 col = mix(vec3(0.06, 0.10, 0.22), vec3(0.10, 0.17, 0.34), uv.y);
    col *= 1.0 - 0.35 * smoothstep(0.55, 1.15, dist);

    // ── Clock face: two crisp rings, beat-locked pulse ──
    float r1 = 0.30 + 0.012 * cos(iBeat * 1.5707963);
    float r2 = 0.56 + 0.010 * cos(iBeat * 0.7853982);
    col += cool(0.28) * ambience * 0.9 * aa(0.0035, abs(dist - r1));
    col += cool(0.50) * ambience * 0.6 * aa(0.0025, abs(dist - r2));
    // ticks each beat around r1 (bar phase aligned)
    float sector = floor(fract(iBeat / 4.0) * 4.0);
    for (int k = 0; k < 4; ++k) {
        float a = TAU * (float(k) / 4.0) - iBeatBarFrac * TAU;
        vec2 dir = vec2(cos(a), sin(a));
        float tick = aa(0.012, abs(dist - r1)) *
                     aa(0.035, abs(atan(p.y, p.x) - a + (a > 3.0 ? -TAU : 0.0)));
        col += cool(0.55) * ambience * tick * 1.1 * (k == int(sector) ? 1.8 : 1.0);
    }

    // ── Accent glyphs: one per upcoming improv fire ──
    for (int i = 0; i < 8; ++i) {
        if (i >= int(iGhostCount)) break;
        vec4 g = (i == 0) ? iGhost0 : (i == 1) ? iGhost1 : (i == 2) ? iGhost2
              : (i == 3) ? iGhost3 : (i == 4) ? iGhost4 : (i == 5) ? iGhost5
              : (i == 6) ? iGhost6 : iGhost7;
        float t01 = 1.0 - clamp(g.z / 4.0, 0.0, 1.0);   // 0 far → 1 firing
        float slot = mod(g.w, 8.0);
        float angle = TAU * (slot / 8.0) + spin * iBeat * 0.2652582
                    + TAU * iBeatBarFrac;
        vec2 dir = vec2(cos(angle), sin(angle));
        vec2 c = dir * mix(0.56, 0.30, t01 * t01);      // fall inward
        vec2 q = (p - c) * rot(-angle);                  // rotate with slot
        float r = 0.055 + 0.06 * g.y * (0.35 + 0.65 * t01);
        float dm = diamond(q, r);
        // fill + outline: bright fill rushes in as the fire nears
        vec3 glow = mix(cool(0.15) * 0.5, vec3(1.05, 1.05, 1.15), t01);
        col += glow * dm * (0.45 + 1.1 * t01) * g.y;
        col += cool(0.85) * aa(r + 0.025, abs(q.x) + abs(q.y)) *
               (1.0 - dm) * (0.5 + 1.2 * t01);
        // inward streak toward centre as it approaches
        float streak = exp(-9.0 * max(0.0, length(p - c) - r)) *
                       (1.0 - exp(-3.0 * (length(p - c) - r + 0.01)));
        col += vec3(0.55, 0.72, 1.0) * streak * t01 * g.y * 0.9;
    }

    fragColor = vec4(col, 1.0);
}

