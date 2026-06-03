-- Scene v2 + MIDI: spawn a copy of the model for each recent note-on,
-- laid out left→right by pitch, spinning + shrinking + fading as it ages,
-- and flashing emissive on the hit. Play notes or run a drum pattern on
-- ANY track and watch them pop. Drop on a clip with a model, then
-- Set Scene Script… → this file.
--
-- ctx.notes = recent note-ons (all tracks): each entry has
--   { track, channel, pitch, vel (0..1), age (seconds) }.
-- They live ~4s in ctx.notes; this script fades them out sooner.

local LIFETIME = 1.2   -- seconds a spawned note stays visible
local SPREAD   = 4.0   -- X spread across the MIDI range (C2..C7)

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
            local life = 1.0 - n.age / LIFETIME            -- 1 → 0 over its life
            local x    = ((n.pitch - 36) / 60 - 0.5) * SPREAD
            local r, g, b = hsv((n.pitch % 12) / 12, 0.75, 1.0)
            out[#out + 1] = {
                -- model = n.pitch % 3,   -- uncomment for per-pitch shapes
                position = { x, (n.vel - 0.5) * 1.5, 0 },
                rotation = { 0, n.age * 360, 0 },          -- spin as it ages
                scale    = (0.25 + 0.5 * n.vel) * life,    -- louder = bigger
                color    = { r, g, b },                    -- hue by pitch class
                emissive = 1.5 * life * life,              -- bright on hit, fades fast
            }
        end
    end
    return out
end
