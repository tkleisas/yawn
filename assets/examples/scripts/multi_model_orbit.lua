-- Scene v2 showcase: multiple models, per-instance colour + emissive,
-- and a returned orbiting camera.
--
-- Drop it on a visual clip that has one or more models:
--   right-click the clip -> Models (N)... -> Add Model...  (add 2-3),
--   then Set Scene Script... -> pick this file.
-- With a single model loaded every slot draws that one model (the
-- `model` index clamps to 0), so it still looks good; with 2-3 models
-- each slot shows a different one. Hot-reloads on save.
--
-- Live controls:
--   knob A = camera distance      knob B = ring spin speed

local SLOTS  = 3       -- instances around the arc
local RADIUS = 1.7     -- arc radius in unit-sphere space

-- Tiny HSV->RGB so each slot gets its own hue.
local function hsv(h, s, v)
    local i = math.floor(h * 6)
    local f = h * 6 - i
    local p = v * (1 - s)
    local q = v * (1 - f * s)
    local t = v * (1 - (1 - f) * s)
    i = i % 6
    if     i == 0 then return v, t, p
    elseif i == 1 then return q, v, p
    elseif i == 2 then return p, v, t
    elseif i == 3 then return p, q, v
    elseif i == 4 then return t, p, v
    else               return v, p, q end
end

function tick(ctx)
    local bands = { ctx.audio.low, ctx.audio.mid, ctx.audio.high }
    local spin  = ctx.time * (20 + 120 * ctx.knobs.B)   -- degrees/sec

    local out = {}
    for i = 0, SLOTS - 1 do
        local a    = (i / SLOTS) * 2 * math.pi + math.rad(spin)
        local band = bands[i + 1] or ctx.audio.level
        local r, g, b = hsv(i / SLOTS, 0.7, 1.0)
        out[#out + 1] = {
            model    = i,                          -- one model per slot
            position = { RADIUS * math.cos(a),
                         -0.2,
                         RADIUS * math.sin(a) * 0.6 },
            rotation = { 0, math.deg(-a) + 90, 0 },
            scale    = 0.5 + 0.35 * band,          -- bounces with its band
            color    = { r, g, b },                -- per-slot hue
            emissive = 0.15 + 1.5 * ctx.audio.kick, -- whole kit flashes on the kick
        }
    end

    -- Orbiting camera. Circles the scene; distance rides on knob A and
    -- pulls back a touch with overall level so big moments breathe.
    local camA = ctx.time * 0.25
    local dist = 4.5 + 4.0 * ctx.knobs.A + 1.5 * ctx.audio.level
    return out, {
        camera = {
            pos    = { dist * math.cos(camA), 1.5, dist * math.sin(camA) },
            target = { 0, 0, 0 },
            fov    = 50,
        }
    }
end
