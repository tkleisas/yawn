local M = {}

M.pitches = {
    kick = 36, snare = 38, clap = 39, tom = 45,
    hat = 42, openhat = 46, ride = 51, crash = 49,
}

local VEL = { X = 118, x = 96, o = 70, g = 45 }

function M.grid(str)
    local out = {}
    for i = 1, math.min(#str, 16) do
        out[i] = VEL[str:sub(i, i)] or 0
    end
    return out
end

function M.groove(rng, style, bars, bpb, energy, swing, fill)
    local lanes = { "kick", "snare", "clap", "hat", "openhat" }
    local stepBeats = bpb / 16
    local total = bars * bpb
    local notes = {}

    for bar = 0, bars - 1 do
        for _, name in ipairs(lanes) do
            local grid = style.drums[name]
            if grid then
                local vel = M.grid(grid)
                for step = 1, 16 do
                    if vel[step] > 0 then
                        local beat = bar * bpb + (step - 1) * stepBeats
                        if step % 2 == 0 then
                            beat = beat + (swing or 0) * stepBeats * 0.5
                        end
                        local v = math.floor(vel[step] * (0.8 + 0.3 * energy) + 0.5)
                        notes[#notes + 1] = {
                            beat = beat, dur = 0.1, pitch = M.pitches[name],
                            vel = math.max(1, math.min(127, v)), ch = 9,
                        }
                    end
                end
            end
        end
    end

    if fill and bars > 0 then
        local start = (bars - 1) * bpb + bpb * 0.5
        for i = 0, 7 do
            notes[#notes + 1] = {
                beat = start + i * stepBeats, dur = 0.1, pitch = 38,
                vel = math.floor(60 + i * 8), ch = 9,
            }
        end
    end

    return { role = "drums", length = total, notes = notes }
end

return M
