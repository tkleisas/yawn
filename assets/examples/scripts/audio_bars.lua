-- Audio bars: a row of bars driven by the spectrum (low on the left →
-- high on the right), scaled vertically, coloured by position, flashing
-- emissive on the kick. A cube model reads best. Static instances, so
-- the whole row is one instanced draw call.

local N     = 9     -- number of bars
local WIDTH = 4.0   -- total spread

function tick(ctx)
    local out = {}
    for i = 0, N - 1 do
        local f = (N > 1) and (i / (N - 1)) or 0.0   -- 0..1 across the row
        -- Blend low→mid→high by position.
        local b
        if f < 0.5 then
            b = ctx.audio.low + (ctx.audio.mid - ctx.audio.low) * (f * 2.0)
        else
            b = ctx.audio.mid + (ctx.audio.high - ctx.audio.mid) * ((f - 0.5) * 2.0)
        end
        local h = 0.15 + 2.5 * b
        out[#out + 1] = {
            position = { (f - 0.5) * WIDTH, h * 0.5 - 0.5, 0 },  -- grow upward
            scale    = { 0.25, h, 0.25 },                        -- per-axis bar
            color    = { f, 0.4 + 0.6 * b, 1.0 - f },
            emissive = 1.2 * ctx.audio.kick,
        }
    end
    return out, { camera = { pos = { 0, 0.8, 5.5 }, target = { 0, 0.3, 0 }, fov = 50 } }
end
