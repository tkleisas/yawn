local M = {}

local function shuffle(rng, n)
    local a = {}
    for i = 1, n do a[i] = i - 1 end
    for i = n, 2, -1 do
        local j = 1 + math.floor(rng.float() * i)
        a[i], a[j] = a[j], a[i]
    end
    return a
end

local function invert(row)
    local out = {}
    for i, pc in ipairs(row) do out[i] = (12 - pc) % 12 end
    return out
end

local function reverse(row, n)
    local out = {}
    for i = 1, n do out[i] = row[n - i + 1] end
    return out
end

-- form: 1 prime, 2 inversion, 3 retrograde, 4 retrograde-inversion
local function formOf(row, idx, form)
    local n = #row
    local base = row
    if form == 2 or form == 4 then base = invert(row) end
    if form == 3 or form == 4 then base = reverse(base, n) end
    return base, idx
end

function M.compose(rng, role, bars, bpb, density, noteLen)
    local total = bars * bpb
    local notes = {}
    local step, octave, ch
    if role == "bass" then
        step = math.max(0.5, noteLen)
        octave = 2
        ch = 0
    elseif role == "chords" then
        step = math.max(1.0, noteLen)
        octave = 3
        ch = 1
    else
        step = math.max(0.25, noteLen)
        octave = 5
        ch = 2
    end

    local row = shuffle(rng, 12)
    local form = 1
    local idx = 1
    local t = 0

    local function pitchOf(pc)
        local p = 60 + pc + 12 * (octave - 4)
        while p > 100 do p = p - 12 end
        while p < 24 do p = p + 12 end
        return p
    end

    while t < total - 1e-9 do
        local r, ri = formOf(row, idx, form)
        local pc = r[ri]
        if rng.float() < density or role == "bass" then
            local dur = math.min(step * 1.2, total - t)
            notes[#notes + 1] = {
                beat = t, dur = dur, pitch = pitchOf(pc), cents = 0,
                vel = math.floor(70 + 30 * rng.float()), ch = ch,
            }
            if role == "chords" then
                local pc2 = r[(ri % 12) + 1]
                notes[#notes + 1] = {
                    beat = t, dur = dur, pitch = pitchOf(pc2), cents = 0,
                    vel = math.floor(60 + 25 * rng.float()), ch = ch,
                }
            end
        end
        idx = idx % 12 + 1
        if idx == 1 then
            form = form % 4 + 1
        end
        t = t + step
    end

    return { role = role, length = total, notes = notes }
end

return M
