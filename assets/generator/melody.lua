local theory = require("theory")
local rhythm = require("rhythm")

local M = {}

local function addNote(notes, beat, dur, pitch, cents, vel, ch, len)
    if beat < 0 then beat = 0 end
    if beat >= len - 1e-6 then return end
    dur = math.max(0.05, math.min(dur, len - beat))
    notes[#notes + 1] = {
        beat = beat, dur = dur, pitch = pitch, cents = cents,
        vel = vel, ch = ch,
    }
end

function M.chordProgression(rng, style, bars, bpb)
    local prog = style.progressions[1 + math.floor(rng.float() * #style.progressions)]
    local degrees = {}
    for bar = 1, bars do
        degrees[bar] = theory.romanDegree(prog[((bar - 1) % #prog) + 1])
    end
    return degrees
end

function M.chords(rng, style, scale, root, chords, bars, bpb, noteLen)
    local notes = {}
    local total = bars * bpb
    local kind = style.chordRhythm or "half"
    local hits
    if kind == "whole" then hits = { 0.0 }
    elseif kind == "quarter" then hits = { 0, 1, 2, 3 }
    elseif kind == "stab" then hits = { 0, 0.75, 1.5, 2.5, 3.25 }
    else hits = { 0, 2 } end

    for bar = 1, bars do
        for _, off in ipairs(hits) do
            if off < bpb then
                local beat = (bar - 1) * bpb + off
                local cn = theory.chord(root, scale, chords[bar], 4, 3)
                for _, n in ipairs(cn) do
                    addNote(notes, beat, math.min(noteLen * 1.8, bpb * 0.9),
                            n.pitch, n.cents, 82, 1, total)
                end
            end
        end
    end

    return { role = "chords", length = total, notes = notes }
end

function M.bass(rng, style, scale, root, chords, bars, bpb, density,
                noteLen, swing, energy)
    local notes = {}
    local total = bars * bpb
    for bar = 1, bars do
        local beat = (bar - 1) * bpb
        local d = chords[bar]
        local p1, c1 = theory.pitch(root, scale, d, 2)
        local p5, c5 = theory.pitch(root, scale, d + 4, 2)
        addNote(notes, beat, math.min(noteLen * 2, bpb * 0.45), p1, c1,
                math.floor(88 + 20 * energy), 0, total)
        if bpb >= 4 then
            addNote(notes, beat + 2, math.min(noteLen * 2, bpb * 0.45),
                    p1, c1, math.floor(78 + 20 * energy), 0, total)
        end
        addNote(notes, beat + bpb - 1, math.min(noteLen, 0.9), p5, c5,
                math.floor(70 + 18 * energy), 0, total)
    end
    return { role = "bass", length = total, notes = notes }
end

function M.lead(rng, style, scale, root, chords, bars, bpb, density,
                noteLen, movement, swing, energy)
    local notes = {}
    local total = bars * bpb
    local onsets = rhythm.pattern(rng, total, density, noteLen)
    local degree = chords[1] + 4

    for _, on in ipairs(onsets) do
        local bar = math.min(bars, math.floor(on.beat / bpb) + 1)
        local pos = on.beat - (bar - 1) * bpb
        local bias = 0
        if movement == "ascending" then
            bias = 1
        elseif movement == "descending" then
            bias = -1
        elseif movement == "arch" then
            bias = (on.beat < total * 0.5) and 1 or -1
        elseif movement == "valley" then
            bias = (on.beat < total * 0.5) and -1 or 1
        end

        local step
        if math.abs(pos - math.floor(pos)) < 1e-9 then
            step = rng.pick({ -2, -1, 0, 1, 2 })
        else
            step = rng.pick({ -1, 0, 1 })
        end
        degree = degree + step + bias

        if math.abs(pos) < 1e-6 or math.abs(pos - 2) < 1e-6 then
            degree = chords[bar] + rng.pick({ 0, 2, 4, 7 })
        end

        local lo = chords[bar] - 2
        local hi = chords[bar] + 12
        if degree < lo then degree = lo end
        if degree > hi then degree = hi end

        local p, c = theory.pitch(root, scale, degree, 5)
        local beat = rhythm.swingBeat(on.beat, swing, 0.5)
        if beat >= total then beat = total - 0.25 end
        local vel = math.floor(76 + 30 * energy + 8 * rng.float())
        addNote(notes, beat, on.dur, p, c, math.min(127, vel), 2, total)
    end

    return { role = "lead", length = total, notes = notes }
end

return M
