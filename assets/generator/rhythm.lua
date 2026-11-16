local M = {}

function M.euclid(pulses, steps, rotation)
    local out = {}
    rotation = rotation or 0
    for i = 0, steps - 1 do
        if (i * pulses) % steps < pulses then
            out[#out + 1] = (i + rotation) % steps
        end
    end
    return out
end

function M.swingBeat(beat, swing, grid)
    grid = grid or 0.5
    if math.abs(beat % grid) > 1e-9 then
        beat = beat + (swing or 0) * grid * 0.5
    end
    return beat
end

-- Onsets for `lengthBeats` at a fixed step grid. On-beat positions are
-- always more likely, so the line keeps a pulse even at low density.
function M.pattern(rng, lengthBeats, density, noteLen, step)
    step = step or 0.25
    local out = {}
    local t = 0
    while t < lengthBeats - 1e-9 do
        local p = density
        if math.abs(t - math.floor(t)) < 1e-9 then
            p = math.min(1.0, p + 0.25)
        end
        if rng.float() < p then
            local dur = noteLen * (0.75 + 0.5 * rng.float())
            dur = math.min(dur, lengthBeats - t)
            if dur > 0.01 then
                out[#out + 1] = { beat = t, dur = dur }
            end
        end
        t = t + step
    end
    if #out == 0 then
        out[1] = { beat = 0, dur = math.min(noteLen, lengthBeats) }
    end
    return out
end

return M
