// Disco ball — a spinning faceted mirror sphere throwing coloured light dots
// around a dark room. MIT License.

uniform float speed;   // @range 0..3   default=0.4  spin speed
uniform float facets;  // @range 6..36  default=16   mirror-tile density
uniform float sparkle; // @range 0..2   default=1.0  tile flash intensity
uniform float hue;     // @range 0..1   default=0.6  light tint
uniform float scale;   // @range 0.3..2.5 default=1.0 ball size

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
vec3 hue2rgb(float h) {
    vec3 k = abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0;
    return clamp(k, 0.0, 1.0);
}
vec3 rotY(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    vec3 ro = vec3(0.0, 0.0, 3.0);
    vec3 rd = normalize(vec3(uv, -1.7));
    // Premultiplied-alpha output so the room is transparent and this can be
    // layered over other visuals (Normal blend). col is already × alpha.
    vec3  col   = vec3(0.0);
    float alpha = 0.0;

    // Scattered coloured light dots behind the ball — glints over whatever is
    // below, slowly revolving.
    {
        float a = iTime * speed * 0.5;
        vec2 q = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
        vec2 g  = q * 7.0;
        vec2 id = floor(g);
        float rr = hash21(id);
        float d  = length(fract(g) - 0.5);
        float dm = smoothstep(0.16, 0.02, d) * step(0.86, rr)
                 * (0.5 + 0.5 * sin(iTime * 3.0 + rr * 6.2831));
        col  += hue2rgb(fract(hue + rr)) * dm;   // premultiplied
        alpha = max(alpha, dm);
    }

    // Ray vs sphere (radius = scale) at the origin.
    float b = dot(ro, rd);
    float c = dot(ro, ro) - scale * scale;
    float h = b * b - c;
    if (h > 0.0) {
        float t   = -b - sqrt(h);
        vec3  pos = ro + rd * t;
        vec3  n   = normalize(pos);
        vec3  nr  = rotY(n, iTime * speed);     // spin the tile grid

        // Lat / long mirror-tile grid.
        float lon = atan(nr.z, nr.x);
        float lat = asin(clamp(nr.y, -1.0, 1.0));
        vec2  g   = vec2(lon, lat) * facets;
        vec2  cell = floor(g);
        vec2  f    = fract(g);
        float rnd  = hash21(cell);

        // Dark grout between tiles.
        float edge = smoothstep(0.0, 0.07, f.x) * smoothstep(0.0, 0.07, 1.0 - f.x)
                   * smoothstep(0.0, 0.07, f.y) * smoothstep(0.0, 0.07, 1.0 - f.y);

        // Per-tile flash + a specular catch from a key light.
        vec3  lightDir = normalize(vec3(0.6, 0.7, 0.6));
        vec3  refl = reflect(rd, n);
        float spec = pow(max(dot(refl, lightDir), 0.0), 8.0);
        float flash = 0.5 + 0.5 * sin(iTime * 5.0 + rnd * 6.2831);
        float tile  = 0.15 + 0.85 * rnd * flash;
        vec3  fcol  = mix(vec3(0.75), hue2rgb(fract(hue + rnd * 0.15)), 0.6);

        vec3 ball = fcol * tile * sparkle + spec * 1.6;
        ball *= edge;
        ball += 0.04;                            // ambient floor
        // Opaque ball, in front of the dots → overwrite (premultiplied: α = 1).
        col   = ball;
        alpha = 1.0;
    }

    fragColor = vec4(col, alpha);
}
