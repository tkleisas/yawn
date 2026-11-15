-- Skull ring — a carousel of the clip's model orbiting a slow central
-- skull, bobbing and pulsing with the kick. Drop on a model clip (the
-- Oxygène skull ring uses this with skull.glb + 38_oxygen_skull.frag's
-- overlay mode). Hot-reload: edit and save; YAWN picks it up next tick.

local N = 12            -- ring copies
local R = 1.55          -- ring radius (unit-sphere space)
local SCALE = 0.30      -- per-skull scale

function tick(ctx)
    local out = {}
    local spin  = ctx.time * 22 + ctx.beat * 2.5
    local pulse = 1.0 + 0.28 * ctx.audio.kick

    for i = 0, N - 1 do
        local a = (i / N) * 2 * math.pi + math.rad(spin)
        out[#out + 1] = {
            position = {
                R * math.cos(a),
                0.10 * math.sin(a * 3 + ctx.time * 0.7),
                R * math.sin(a),
            },
            -- tangential facing, like a carousel of watchers
            rotation = { 0, math.deg(-a) + 90, 0 },
            scale    = SCALE * pulse,
            emissive = 0.15 + 0.25 * ctx.audio.kick,
        }
    end

    -- the still centre: one larger skull turning slowly on its own
    out[#out + 1] = {
        position = { 0.0, 0.0, 0.0 },
        rotation = { 0, ctx.time * 8, 0 },
        scale    = 0.62 * (1.0 + 0.06 * ctx.audio.kick),
        emissive = 0.25 + 0.35 * ctx.audio.kick,
    }
    return out
end
