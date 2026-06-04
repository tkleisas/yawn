-- Note glitch ghosts — every MIDI note-on spawns a displaced, rotated
-- "ghost" copy of the model that smears outward and fades by age, like a
-- datamoshed frame echo. The base model sits centre and shivers on the
-- kick. Play notes on ANY track. Drop on a model clip, Set Scene Script…
--
-- ctx.notes = recent note-ons (all tracks): each entry has
--   { track, channel, pitch, vel (0..1), age (seconds) }, newest last.

local LIFE = 0.9   -- seconds a ghost stays visible

local function hash(n) return (math.sin(n * 73.1) * 43758.5453) % 1.0 end

function tick(ctx)
    local out = {}

    -- Base model — centre, jittering hard on the kick (datamosh shiver).
    local k = ctx.audio.kick
    out[#out + 1] = {
        position = { (hash(ctx.beat * 30.0)       - 0.5) * k * 0.4,
                     (hash(ctx.beat * 30.0 + 9.0) - 0.5) * k * 0.4,
                     0.0 },
        rotation = { 0, ctx.time * 20.0, 0 },
        scale    = 1.0 + k * 0.3,
        emissive = k * 1.2,
    }

    -- One ghost per recent note, flung out by pitch class and fading by age.
    for _, n in ipairs(ctx.notes) do
        if n.age < LIFE then
            local life = 1.0 - n.age / LIFE                 -- 1 → 0 over its life
            local dir  = (n.pitch % 12) / 12.0 * 2.0 * math.pi
            local dist = (0.6 + 1.4 * (1.0 - life)) * (0.5 + n.vel)  -- smears out
            local h    = hash(n.pitch + n.track * 7.0)
            out[#out + 1] = {
                position = { math.cos(dir) * dist,
                             math.sin(dir) * dist,
                             (h - 0.5) * 1.0 },
                rotation = { n.age * 540.0 * (h + 0.3),
                             n.age * 360.0,
                             math.floor(h * 4.0) * 90.0 },
                scale    = (0.5 + 0.5 * n.vel) * (0.4 + 0.6 * life),
                opacity  = life,                            -- fade out (BLEND mats)
                emissive = 1.4 * life,
                color    = { 0.6 + 0.4 * h, 0.4, 1.0 - 0.4 * h },
            }
        end
    end

    return out, {
        camera = { pos = { 0, 0, 4.5 }, target = { 0, 0, 0 }, fov = 55 }
    }
end
