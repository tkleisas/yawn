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

local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

function M.chordProgression(rng, style, bars, bpb)
    local prog = style.progressions[1 + math.floor(rng.float() * #style.progressions)]
    local degrees = {}
    for bar = 1, bars do
        degrees[bar] = theory.romanDegree(prog[((bar - 1) % #prog) + 1])
    end
    return degrees
end

local function hitsFor(kind)
    if kind == "whole" or kind == "drone" then return { 0.0 } end
    if kind == "quarter" then return { 0, 1, 2, 3 } end
    if kind == "offbeat" then return { 0.5, 1.5, 2.5, 3.5 } end
    if kind == "stab" then return { 0, 0.75, 1.5, 2.5, 3.25 } end
    if kind == "16th" then
        return { 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5 }
    end
    return { 0, 2 }
end

local function voicing(style, root, scale, degree, octave)
    if style.chordVoicing == "power" then
        local p1, c1 = theory.pitch(root, scale, degree, octave)
        local p5, c5 = theory.pitch(root, scale, degree + 4, octave)
        local p8, c8 = theory.pitch(root, scale, degree + 7, octave + 1)
        return {
            { pitch = p1, cents = c1 },
            { pitch = p5, cents = c5 },
            { pitch = p8, cents = c8 },
        }
    end
    if style.chordVoicing == "drone" then
        local p1, c1 = theory.pitch(root, scale, degree, octave)
        local p5, c5 = theory.pitch(root, scale, degree + 4, octave)
        return {
            { pitch = p1, cents = c1 },
            { pitch = p5, cents = c5 },
        }
    end
    return theory.chord(root, scale, degree, octave, style.chordSize or 3)
end

function M.chords(rng, style, scale, root, chords, bars, bpb, noteLen)
    local notes = {}
    local total = bars * bpb
    local octave = style.chordOctave or 4
    local kind = style.chordRhythm or "half"
    local hits = hitsFor(kind)

    for bar = 1, bars do
        for _, off in ipairs(hits) do
            if off < bpb then
                local beat = (bar - 1) * bpb + off
                local cn = voicing(style, root, scale, chords[bar], octave)
                local dur = (kind == "drone")
                    and (bpb * 0.98)
                    or math.min(noteLen * 1.8, bpb * 0.9)
                for _, n in ipairs(cn) do
                    addNote(notes, beat, dur, n.pitch, n.cents, 82, 1, total)
                end
            end
        end
    end

    return { role = "chords", length = total, notes = notes }
end

local function bassNote(notes, beat, dur, pitch, cents, vel, total)
    addNote(notes, beat, dur, pitch, cents, clamp(vel, 1, 127), 0, total)
end

function M.bass(rng, style, scale, root, chords, bars, bpb, density,
                noteLen, swing, energy)
    local notes = {}
    local total = bars * bpb
    local kind = style.bassStyle or "root"
    local octave = style.bassOctave or 2
    local velBase = math.floor(84 + 24 * energy)

    for bar = 1, bars do
        local beat0 = (bar - 1) * bpb
        local d = chords[bar]
        local p1, c1 = theory.pitch(root, scale, d, octave)
        local p5, c5 = theory.pitch(root, scale, d + 4, octave)

        if kind == "walk" then
            for i = 0, bpb - 1 do
                local deg
                if i == bpb - 1 then
                    deg = d + rng.pick({ -1, 1, 2, 4, 5 })
                else
                    deg = d + ({ 0, 2, 4, 6 })[(i % 4) + 1]
                end
                local p, c = theory.pitch(root, scale, deg, octave)
                bassNote(notes, beat0 + i, 0.9, p, c, velBase - 4, total)
            end
        elseif kind == "arp" then
            local seq = { d, d + 2, d + 4, d + 7 }
            for i = 0, bpb - 1 do
                local deg = seq[(i % #seq) + 1]
                local p, c = theory.pitch(root, scale, deg, octave)
                bassNote(notes, beat0 + i, math.min(noteLen, 0.9), p, c,
                         velBase - 6, total)
            end
        elseif kind == "drone" then
            bassNote(notes, beat0, bpb * 0.98, p1, c1, velBase - 10, total)
            bassNote(notes, beat0, bpb * 0.98, p5, c5, velBase - 24, total)
        elseif kind == "chug" then
            local step = 0.25
            local t = 0
            local i = 0
            while t < bpb - 1e-9 do
                local accent = (i % 4 == 0) and 8 or 0
                addNote(notes, beat0 + t, 0.18, p1, c1,
                        clamp(velBase + accent, 1, 127), 0, total)
                t = t + step
                i = i + 1
            end
        elseif kind == "eighths" then
            local n8 = math.max(1, math.floor(bpb * 2))
            for i = 0, n8 - 1 do
                local deg = (i % 4 == 3) and (d + 4) or d
                local p, c = theory.pitch(root, scale, deg, octave)
                addNote(notes, beat0 + i * 0.5, 0.4, p, c,
                        clamp(velBase - (i % 2) * 8, 1, 127), 0, total)
            end
        elseif kind == "offbeat" then
            for _, off in ipairs({ 0.5, 1.5, 2.5, 3.5 }) do
                if off < bpb then
                    addNote(notes, beat0 + off, math.min(noteLen, 0.45),
                            p1, c1, velBase - 6, 0, total)
                end
            end
        else
            bassNote(notes, beat0, math.min(noteLen * 2, bpb * 0.45),
                     p1, c1, velBase, total)
            if bpb >= 4 then
                bassNote(notes, beat0 + 2, math.min(noteLen * 2, bpb * 0.45),
                         p1, c1, velBase - 10, total)
            end
            bassNote(notes, beat0 + bpb - 1, math.min(noteLen, 0.9),
                     p5, c5, velBase - 18, total)
        end
    end

    return { role = "bass", length = total, notes = notes }
end

function M.lead(rng, style, scale, root, chords, bars, bpb, density,
                noteLen, movement, swing, energy)
    local notes = {}
    local total = bars * bpb
    local onsets = rhythm.pattern(rng, total, density, noteLen)
    local degree = chords[1] + 4
    local ornament = (style.melody and style.melody.ornament) or 0
    local reuse = style.motifRepeat or 0
    local motif = {}
    local motifLen = math.min(2, bars) * bpb

    for i, on in ipairs(onsets) do
        local bar = clamp(math.floor(on.beat / bpb) + 1, 1, bars)
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
        local dir = (step + bias) >= 0
        local target = degree + step + bias

        if math.abs(pos) < 1e-6 or math.abs(pos - 2) < 1e-6 then
            target = chords[bar] + rng.pick({ 0, 2, 4, 7 })
        end

        if #motif > 0 and on.beat >= motifLen and rng.float() < reuse then
            local off = motif[(i - 1) % #motif + 1]
            target = chords[bar] + off
        end

        target = clamp(target, chords[bar] - 2, chords[bar] + 12)
        degree = theory.nearestAllowed(scale, target, dir)

        local p, c = theory.pitch(root, scale, degree, 5)
        local beat = rhythm.swingBeat(on.beat, swing, 0.5)
        if beat >= total then beat = total - 0.25 end
        local vel = math.floor(76 + 30 * energy + 8 * rng.float())
        addNote(notes, beat, on.dur, p, c, math.min(127, vel), 2, total)

        if on.beat < motifLen and #motif < 32 then
            motif[#motif + 1] = degree - chords[bar]
        end

        if ornament > 0 and beat > 0.2 and rng.float() < ornament then
            local gdeg = theory.nearestAllowed(scale, degree - 1, false)
            local gp, gc = theory.pitch(root, scale, gdeg, 5)
            addNote(notes, beat - 0.125, 0.1, gp, gc,
                    math.max(1, vel - 28), 2, total)
        end
    end

    return { role = "lead", length = total, notes = notes }
end

return M
