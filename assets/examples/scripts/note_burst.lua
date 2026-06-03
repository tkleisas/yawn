-- Scene v2 + MIDI: spawn a copy of the model for each recent note-on,
-- arranged on a centred ring by pitch-class (so drums, bass, and leads
-- all stay framed — low drum pitches don't pile up off-screen), popping
-- big on the hit then spinning, shrinking, and fading as they age.
--
-- Play notes or a drum pattern on ANY track. Drop on a clip with a
-- model, then Set Scene Script… → this file.
--
-- ctx.notes = recent note-ons (all tracks): each entry has
--   { track, channel, pitch, vel (0..1), age (seconds) }.

local LIFETIME = 1.2   -- seconds a spawned note stays visible
local RING     = 1.3   -- ring radius in unit-sphere space

local function hsv(h, s, v)
    local i = math.floor(h * 6)
    local f = h * 6 - i
    local p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    i = i % 6
    if     i == 0 then return v, t, p
    elseif i == 1 then return q, v, p
    elseif i == 2 then return p, v, t
    elseif i == 3 then return p, q, v
    elseif i == 4 then return t, p, v
    else               return v, p, q end
end

function tick(ctx)
    local out = {}
    for _, n in ipairs(ctx.notes) do
        if n.age < LIFETIME then
            local life = 1.0 - n.age / LIFETIME          -- 1 → 0 over its life
            local pc   = n.pitch % 12                    -- pitch class → angle
            local a    = (pc / 12) * 2 * math.pi
            local oct  = math.floor(n.pitch / 12) - 5    -- octave → depth
            local pop  = 1.0 + 0.6 * life * life         -- snap big on the hit
            local r, g, b = hsv(pc / 12, 0.75, 1.0)
            out[#out + 1] = {
                position = { RING * math.cos(a),
                             RING * math.sin(a),
                             oct * 0.35 },
                rotation = { 0, n.age * 360, 0 },         -- spin as it ages
                scale    = (0.18 + 0.32 * n.vel) * life * pop,
                color    = { r, g, b },                   -- hue by pitch class
                emissive = 1.6 * life * life,             -- bright on hit, fades fast
            }
        end
    end

    -- Pull the camera back far enough to frame the whole ring head-on.
    return out, {
        camera = { pos = { 0, 0, 4.2 }, target = { 0, 0, 0 }, fov = 55 }
    }
end
