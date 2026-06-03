-- Spin helix: a vertical helix of copies, the whole thing turning, each
-- copy breathing with the audio level and flashing on the kick. Static
-- instances, so all 24 copies are a single instanced draw.

local N      = 24    -- copies
local RADIUS = 1.1
local HEIGHT = 3.0
local TURNS  = 3

function tick(ctx)
    local out  = {}
    local turn = ctx.time * 40.0        -- whole helix rotates, deg/s
    for i = 0, N - 1 do
        local f = i / N
        local a = f * (2.0 * math.pi) * TURNS + math.rad(turn)
        out[#out + 1] = {
            position = { RADIUS * math.cos(a), (f - 0.5) * HEIGHT, RADIUS * math.sin(a) },
            rotation = { 0, math.deg(a), 0 },
            scale    = 0.18 + 0.12 * ctx.audio.level,
            color    = { 0.5 + 0.5 * math.cos(a), 0.5, 0.5 + 0.5 * math.sin(a) },
            emissive = 0.4 * ctx.audio.kick,
        }
    end
    return out, { camera = { pos = { 0, 0, 5 }, target = { 0, 0, 0 }, fov = 55 } }
end
