-- Ring of copies of the clip's model, breathing on the kick.
-- Drop this on any model clip (with the Duck or anything else).
-- Hot-reload: edit and save — YAWN picks up changes next tick.

local N = 8                      -- number of copies around the ring
local baseRadius = 1.6           -- ring radius in unit-sphere space

function tick(ctx)
    local out = {}
    local spin = ctx.time * 45    -- whole ring slowly rotates
    local pulse = 1.0 + 0.5 * ctx.audio.kick
    for i = 0, N - 1 do
        local a = (i / N) * 2 * math.pi + math.rad(spin)
        out[#out + 1] = {
            position = { baseRadius * math.cos(a),
                         0.0,
                         baseRadius * math.sin(a) * 0.5 },  -- squash z
            rotation = { 0, math.deg(-a) + 90, 0 },
            scale    = 0.45 * pulse,
        }
    end
    return out
end
