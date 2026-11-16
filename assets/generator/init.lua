local drums = require("drums")
local melody = require("melody")
local sections = require("sections")
local blend = require("blend")
local serial = require("serial")

local M = {}

local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

local DEFAULT_LANES = { "drums", "bass", "chords", "lead" }

local function styleLanes(style)
    return style.lanes or DEFAULT_LANES
end

function M.generate(params)
    local rng = yawn.rng
    local entries = blend.load(rng, params.styles)
    local mode = params.blend or "fuse"

    local first = blend.pick(rng, entries)
    local template
    if params.sections and #params.sections > 0 then
        template = params.sections
    else
        template = first.style.sections or sections.defaultTemplate
    end
    local plan = sections.plan(template)

    local tempo = params.tempo
    if not tempo or tempo <= 0 then
        tempo = math.floor(blend.mean(entries, "tempo", 120) + 0.5)
    end
    local bpb = params.beatsPerBar or 4

    local userDensity = params.density and params.density >= 0
    local userNoteLen = params.noteLength and params.noteLength >= 0
    local userSwing = params.swing and params.swing >= 0

    local fuseDensity = userDensity and params.density
        or blend.mean(entries, "density", 0.55)
    local fuseNoteLen = userNoteLen and params.noteLength
        or blend.mean(entries, "noteLength", 0.5)
    local fuseSwing = userSwing and params.swing
        or blend.mean(entries, "swing", 0.0)

    local song = {
        title = "Generated",
        tempo = tempo,
        beatsPerBar = bpb,
        sections = {},
    }

    for _, sec in ipairs(plan) do
        local style = blend.pick(rng, entries).style

        local density, noteLen, swing
        if mode == "rotate" then
            density = userDensity and params.density
                or blend.range(style, "density", rng, 0.55)
            noteLen = userNoteLen and params.noteLength
                or blend.range(style, "noteLength", rng, 0.5)
            swing = userSwing and params.swing or (style.swing or 0.0)
        else
            density = fuseDensity
            noteLen = fuseNoteLen
            swing = fuseSwing
        end
        density = clamp(density * (0.8 + 0.4 * sec.energy), 0.03, 1.0)
        noteLen = clamp(noteLen, 0.1, 2.0)
        swing = clamp(swing, 0.0, 0.75)

        local lanes = styleLanes(style)
        local clips = {}

        if style.serial then
            for _, lane in ipairs(lanes) do
                clips[#clips + 1] = serial.compose(
                    rng, lane, sec.bars, bpb, density, noteLen)
            end
        else
            local scale = (params.scale and params.scale ~= "")
                and params.scale
                or style.scales[1 + math.floor(rng.float() * #style.scales)]
            local root = (params.root and params.root ~= "")
                and params.root
                or ((style.roots and style.roots[1 + math.floor(rng.float() * #style.roots)]) or "C")
            local movement = params.movement
            if not movement or movement == "auto" or movement == "" then
                movement = (style.melody and style.melody.contour) or "arch"
            end

            local chords = melody.chordProgression(
                rng, style, sec.bars, bpb)

            for _, lane in ipairs(lanes) do
                if lane == "drums" then
                    clips[#clips + 1] = drums.groove(
                        rng, style, sec.bars, bpb, sec.energy, swing,
                        sec.id == "chorus" or sec.id == "drop")
                elseif lane == "bass" then
                    clips[#clips + 1] = melody.bass(
                        rng, style, scale, root, chords, sec.bars, bpb,
                        density, noteLen, swing, sec.energy)
                elseif lane == "chords" then
                    clips[#clips + 1] = melody.chords(
                        rng, style, scale, root, chords, sec.bars, bpb,
                        noteLen)
                elseif lane == "lead" then
                    clips[#clips + 1] = melody.lead(
                        rng, style, scale, root, chords, sec.bars, bpb,
                        density, noteLen, movement, swing, sec.energy)
                end
            end
        end

        song.sections[#song.sections + 1] = {
            id = sec.id,
            name = sec.name,
            bars = sec.bars,
            clips = clips,
        }
    end

    return song
end

return M
