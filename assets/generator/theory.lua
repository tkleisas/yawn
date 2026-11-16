local M = {}

M.roots = {
    C = 0, ["C#"] = 1, Db = 1, D = 2, ["D#"] = 3, Eb = 3, E = 4, F = 5,
    ["F#"] = 6, Gb = 6, G = 7, ["G#"] = 8, Ab = 8, A = 9, ["A#"] = 10,
    Bb = 10, B = 11,
}

-- Scale degrees in semitones. Floats express cents-based tuning
-- (e.g. rast's neutral third is 3.5 semitones = 350 cents).
M.scales = {
    major = { 0, 2, 4, 5, 7, 9, 11 },
    natural_minor = { 0, 2, 3, 5, 7, 8, 10 },
    harmonic_minor = { 0, 2, 3, 5, 7, 8, 11 },
    melodic_minor = { 0, 2, 3, 5, 7, 9, 11 },
    dorian = { 0, 2, 3, 5, 7, 9, 10 },
    phrygian = { 0, 1, 3, 5, 7, 8, 10 },
    lydian = { 0, 2, 4, 6, 7, 9, 11 },
    mixolydian = { 0, 2, 4, 5, 7, 9, 10 },
    locrian = { 0, 1, 3, 5, 6, 8, 10 },
    pentatonic_major = { 0, 2, 4, 7, 9 },
    pentatonic_minor = { 0, 3, 5, 7, 10 },
    blues = { 0, 3, 5, 6, 7, 10 },
    hijaz = { 0, 1, 4, 5, 7, 8, 10 },
    rast = { 0, 2, 3.5, 5, 7, 9, 10.5 },
    bayati = { 0, 1.5, 3, 5, 7, 8.5, 10 },
    saba = { 0, 1.5, 3, 4, 7, 8, 10 },
    kurd = { 0, 1, 3, 5, 7, 8, 10 },
    nahawand = { 0, 2, 3, 5, 7, 8, 10 },
    raga_bhairav = { 0, 1, 4, 5, 7, 8, 11 },
    raga_yaman = { 0, 2, 4, 6, 7, 9, 11 },
    raga_bhairavi = { 0, 1, 3, 5, 7, 8, 10 },
}

M.scaleIds = {}
for id in pairs(M.scales) do M.scaleIds[#M.scaleIds + 1] = id end
table.sort(M.scaleIds)

local ROMAN = { I = 0, II = 1, III = 2, IV = 3, V = 4, VI = 5, VII = 6 }

function M.scale(id)
    local s = M.scales[id]
    if not s then error("unknown scale '" .. tostring(id) .. "'") end
    return s
end

function M.rootSemitone(root)
    local v = M.roots[root]
    if v then return v end
    v = tonumber(root)
    if v then return v % 12 end
    error("unknown root note '" .. tostring(root) .. "'")
end

-- Absolute semitone value with C4 = 60. `degree` may be negative or
-- beyond one octave; fractional degrees follow the scale's cents.
function M.semitone(root, scaleId, degree, octave)
    local s = M.scale(scaleId)
    local n = #s
    local oct = math.floor(degree / n)
    local idx = degree - oct * n
    return 60 + M.rootSemitone(root) + s[idx + 1]
         + 12 * ((octave or 4) - 4 + oct)
end

-- Returns (midiPitch, cents) for the nearest semitone + remainder.
function M.pitch(root, scaleId, degree, octave)
    local semis = M.semitone(root, scaleId, degree, octave)
    local pitch = math.floor(semis + 0.5)
    local cents = (semis - pitch) * 100.0
    return pitch, cents
end

function M.romanDegree(token)
    local s = tostring(token):match("^[iIvV]+")
    if not s then error("bad roman numeral '" .. tostring(token) .. "'") end
    local d = ROMAN[s:upper()]
    if not d then error("bad roman numeral '" .. tostring(token) .. "'") end
    return d
end

function M.chord(root, scaleId, degree, octave, size)
    local out = {}
    for i = 0, (size or 3) - 1 do
        local p, c = M.pitch(root, scaleId, degree + 2 * i, octave)
        out[#out + 1] = { pitch = p, cents = c }
    end
    return out
end

return M
