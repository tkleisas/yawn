local M = {}

local SEED_IMAGE = "seed_emblem.png"

local NEEDS_IMAGE = {
    ["32_mandala_seed"] = true,
    ["34_eye_field"] = true,
    ["35_temple_mist"] = true,
    ["36_ink_brush"] = true,
    ["37_infinite_zoom"] = true,
}

local THEMES = {
    sacred = {
        calm = { "35_temple_mist", "36_ink_brush" },
        drive = { "40_sri_yantra", "32_mandala_seed", "34_eye_field" },
        peak = { "40_sri_yantra", "32_mandala_seed" },
    },
    cosmic = {
        calm = { "45_starfield" },
        drive = { "43_cymatics", "41_black_sun", "47_gargantua" },
        peak = { "41_black_sun", "47_gargantua", "45_starfield" },
    },
    ink = {
        calm = { "35_temple_mist" },
        drive = { "36_ink_brush", "44_rorschach" },
        peak = { "36_ink_brush", "44_rorschach" },
    },
    abstract = {
        calm = { "45_starfield", "27_hypno_spiral" },
        drive = { "42_sigil_seal", "43_cymatics", "37_infinite_zoom" },
        peak = { "42_sigil_seal", "43_cymatics", "37_infinite_zoom" },
    },
}


-- Per-theme, per-lane shader pools. Drums are the background layer;
-- bass/chords/lead layer over it with transparent or additive shaders.
local ROLE_THEMES = {
    sacred = {
        drums = { "43_cymatics", "45_starfield", "14_kaleidoscope" },
        bass = { "47_gargantua", "37_infinite_zoom", "10_tunnel" },
        chords = { "32_mandala_seed", "40_sri_yantra", "34_eye_field",
                   "35_temple_mist", "36_ink_brush" },
        lead = { "34_eye_field", "40_sri_yantra", "36_ink_brush",
                 "45_starfield" },
    },
    cosmic = {
        drums = { "43_cymatics", "41_black_sun", "45_starfield",
                  "14_kaleidoscope" },
        bass = { "47_gargantua", "37_infinite_zoom", "10_tunnel",
                 "16_aurora" },
        chords = { "45_starfield", "41_black_sun", "42_sigil_seal",
                   "43_cymatics" },
        lead = { "45_starfield", "43_cymatics", "47_gargantua",
                 "16_aurora" },
    },
    ink = {
        drums = { "14_kaleidoscope", "43_cymatics", "45_starfield" },
        bass = { "10_tunnel", "16_aurora", "37_infinite_zoom" },
        chords = { "35_temple_mist", "36_ink_brush", "44_rorschach",
                   "32_mandala_seed" },
        lead = { "36_ink_brush", "44_rorschach", "35_temple_mist",
                 "45_starfield" },
    },
    abstract = {
        drums = { "14_kaleidoscope", "43_cymatics", "15_pulse_grid",
                  "45_starfield" },
        bass = { "37_infinite_zoom", "10_tunnel", "27_hypno_spiral",
                 "16_aurora" },
        chords = { "42_sigil_seal", "43_cymatics", "44_rorschach",
                   "40_sri_yantra" },
        lead = { "42_sigil_seal", "27_hypno_spiral", "37_infinite_zoom",
                 "45_starfield" },
    },
}

local ROLE_REACT = { drums = 1.25, bass = 0.9, chords = 0.7, lead = 1.0 }

local AFFINITY = {
    pop = { "abstract", "cosmic" },
    rock = { "cosmic", "abstract" },
    classical = { "ink", "sacred" },
    techno = { "abstract", "cosmic" },
    house = { "abstract", "cosmic" },
    jazz = { "ink" },
    metal = { "cosmic" },
    ethnic_maqam = { "sacred" },
    raga = { "sacred" },
    serial = { "abstract" },
}

local PALETTE = {
    sacred = 0.75,
    cosmic = 0.30,
    ink = 0.20,
    abstract = 0.50,
}

local OVERRIDES = {
    ["32_mandala_seed"] = {
        segments = function(rng) return rng.pick({ 8, 10, 12 }) end,
        petals = function(rng) return rng.pick({ 6, 8, 10 }) end,
        rings = function(rng) return rng.pick({ 3, 4 }) end,
        gold = function(rng, e) return 0.55 + 0.35 * e end,
        palette = function() return 0.75 end,
        spin = function(rng, e) return 0.2 + 0.6 * e end,
        glow = function(rng, e) return 0.45 + 0.45 * e end,
        zoom = function() return 1.0 end,
    },
    ["34_eye_field"] = {
        eyes = function(rng) return rng.pick({ 4, 5, 6 }) end,
        rings = function(rng) return rng.pick({ 2, 3 }) end,
        radius = function() return 0.32 end,
        spread = function() return 0.55 end,
        size = function() return 0.5 end,
        open = function(rng, e) return 0.55 + 0.3 * e end,
        gold = function() return 0.72 end,
        palette = function() return 0.75 end,
        spin = function(rng, e) return 0.15 + 0.45 * e end,
    },
    ["35_temple_mist"] = {
        layers = function() return 4 end,
        pagodas = function() return 3 end,
        peaks = function() return 0.5 end,
        mist = function(rng, e) return 0.35 + 0.3 * e end,
        lanterns = function(rng, e) return 0.5 + 0.4 * e end,
        brush = function() return 0.6 end,
        sun = function() return 0.7 end,
        palette = function() return 0.2 end,
        drift = function(rng, e) return 0.2 + 0.4 * e end,
    },
    ["36_ink_brush"] = {
        enso = function() return 0.8 end,
        grain = function() return 0.55 end,
        strokes = function(rng, e) return 0.35 + 0.35 * e end,
        splatter = function(rng, e) return 0.25 + 0.35 * e end,
        seal = function() return 0.5 end,
        palette = function() return 0.2 end,
        spin = function(rng, e) return 0.1 + 0.3 * e end,
    },
    ["37_infinite_zoom"] = {
        folds = function(rng) return rng.pick({ 6, 8 }) end,
        speed = function(rng, e) return 0.08 + 0.22 * e end,
        scales = function() return 4.0 end,
        twist = function() return 0.2 end,
        palette = function() return 0.5 end,
        glow = function(rng, e) return 0.45 + 0.4 * e end,
    },
    ["40_sri_yantra"] = {
        size = function() return 0.85 end,
        gold = function(rng, e) return 0.65 + 0.25 * e end,
        palette = function() return 0.75 end,
        glow = function(rng, e) return 0.5 + 0.4 * e end,
        spin = function(rng, e) return 1.2 + 2.2 * e end,
        kick = function() return 0.6 end,
    },
    ["41_black_sun"] = {
        disc = function() return 0.35 end,
        corona = function(rng, e) return 0.65 + 0.3 * e end,
        rays = function() return 0.6 end,
        glint = function() return 0.7 end,
        palette = function() return 0.3 end,
        kick = function() return 0.55 end,
    },
    ["42_sigil_seal"] = {
        cells = function(rng) return rng.pick({ 9, 12 }) end,
        radial = function(rng) return rng.pick({ 0, 1 }) end,
        ink = function() return 0.85 end,
        palette = function() return 0.5 end,
        glow = function(rng, e) return 0.4 + 0.4 * e end,
        kick = function() return 0.5 end,
    },
    ["43_cymatics"] = {
        modeN = function() return 3 end,
        modeM = function() return 4 end,
        spread = function() return 0.6 end,
        grain = function() return 0.4 end,
        kick = function() return 0.65 end,
        palette = function() return 0.5 end,
    },
    ["44_rorschach"] = {
        ink = function() return 0.8 end,
        scale = function() return 1.0 end,
        warp = function() return 0.5 end,
        soft = function() return 0.4 end,
        palette = function() return 0.3 end,
        kick = function() return 0.55 end,
    },
    ["45_starfield"] = {
        density = function() return 0.8 end,
        drift = function(rng, e) return 0.15 + 0.4 * e end,
        nebula = function(rng, e) return 0.25 + 0.25 * e end,
        twinkle = function() return 0.6 end,
        palette = function() return 0.3 end,
    },
    ["47_gargantua"] = {
        disk = function() return 0.9 end,
        incl = function() return 0.18 end,
        tilt = function() return -6 end,
        spin = function() return 1.0 end,
        photon = function() return 0.85 end,
        lens = function() return 1.0 end,
        stars = function() return 0.55 end,
        kick = function() return 0.4 end,
        palette = function() return 0.5 end,
    },
    ["27_hypno_spiral"] = {
        arms = function(rng) return rng.pick({ 4, 6, 8 }) end,
        color1 = function() return 0.55 end,
        color2 = function() return 0.9 end,
        centerRadius = function() return 0.05 end,
        waviness = function() return 0.3 end,
        speed = function(rng, e) return 1.0 + 2.5 * e end,
    },
    ["14_kaleidoscope"] = {
        segments = function(rng) return rng.pick({ 6, 8, 12 }) end,
        rotate = function(rng, e) return 0.1 + 0.3 * e end,
        zoom = function() return 1.1 end,
    },
    ["16_aurora"] = {
        bands = function() return 4 end,
        speed = function(rng, e) return 0.4 + 0.8 * e end,
        bright = function(rng, e) return 0.45 + 0.4 * e end,
    },
}

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function pretty(id)
    local s = id:gsub("^%d+_", ""):gsub("_", " ")
    return (s:gsub("(%a)([%w']*)", function(a, b)
        return a:upper() .. b
    end))
end

local function chooseTheme(rng, params, entries)
    local t = params.visualTheme
    if t and t ~= "" and t ~= "auto" and THEMES[t] then return t end
    if not entries or #entries == 0 then return "abstract" end
    local choice = entries[1]
    local total, acc = 0, 0
    for _, e in ipairs(entries) do total = total + (e.norm or 1) end
    local r = rng.float() * (total > 0 and total or 1)
    for _, e in ipairs(entries) do
        acc = acc + (e.norm or 1)
        choice = e
        if r <= acc then break end
    end
    local aff = AFFINITY[choice.id] or { "abstract" }
    return aff[1 + math.floor(rng.float() * #aff)]
end

local function available(catalog)
    local byId = {}
    for _, c in ipairs(catalog or {}) do byId[c.id] = c end
    return byId
end

local function pickRole(rng, ids, byId, used)
    if not ids then return nil end
    local free = {}
    for _, id in ipairs(ids) do
        if byId[id] and not used[id] then free[#free + 1] = id end
    end
    if #free == 0 then
        for _, id in ipairs(ids) do
            if byId[id] then free[#free + 1] = id end
        end
    end
    if #free == 0 then return nil end
    local id = free[1 + math.floor(rng.float() * #free)]
    used[id] = true
    return id
end

local function makeParams(rng, entry, energy, theme)
    local out = {}
    local ov = OVERRIDES[entry.id] or {}
    local palette = PALETTE[theme] or 0.5
    for _, pi in ipairs(entry.params or {}) do
        local name = pi.name
        local lo = pi.min or 0
        local hi = pi.max or 1
        if hi <= lo then hi = lo + 1e-6 end
        local def = pi["default"] or lo
        local v
        if ov[name] then
            v = ov[name](rng, energy)
        else
            local ln = name:lower()
            if ln:find("seed") then
                v = lo + rng.float() * (hi - lo)
            elseif ln:find("palette") then
                v = palette
            elseif ln:find("kick") or ln:find("flash") then
                v = lo + (hi - lo) * (0.4 + 0.45 * energy)
            elseif ln:find("glow") or ln:find("bright") or
                   ln:find("intensity") then
                v = lo + (hi - lo) * (0.35 + 0.55 * energy)
            elseif ln:find("spin") or ln:find("speed") or
                   ln:find("drift") or ln:find("flow") then
                v = lo + (hi - lo) * (0.25 + 0.6 * energy)
            elseif ln == "segments" or ln == "petals" or ln == "rings" or
                   ln == "arms" or ln == "folds" or ln == "cells" or
                   ln == "eyes" or ln == "layers" or ln == "bands" then
                v = math.floor(lo + (hi - lo) * (0.4 + 0.2 * rng.float()) + 0.5)
            elseif ln == "zoom" or ln == "scale" then
                v = lo + (hi - lo) * 0.45
            else
                v = def + (rng.float() - 0.5) * 0.16 * (hi - lo)
            end
        end
        out[name] = clamp(v, lo, hi)
    end
    return out
end

function M.plan(rng, params, sections, entries, catalog)
    local byId = available(catalog)
    local theme = chooseTheme(rng, params, entries)
    local def = THEMES[theme] or THEMES.abstract
    local roleTheme = ROLE_THEMES[theme] or ROLE_THEMES.abstract
    local plan = {}

    for i, sec in ipairs(sections) do
        local energy = clamp(sec.energy or 0.5, 0, 1)
        local bucket = "drive"
        if sec.id == "intro" or sec.id == "outro" then
            bucket = "calm"
        elseif sec.id == "chorus" or sec.id == "drop" then
            bucket = "peak"
        end

        -- One visual per audio lane, in lane order.
        local roles, seenRole = {}, {}
        for _, clip in ipairs(sec.clips or {}) do
            local role = clip.role or "visual"
            if not seenRole[role] then
                seenRole[role] = true
                roles[#roles + 1] = role
            end
        end
        if #roles == 0 then roles = { "visual" } end

        local used, visuals = {}, {}
        for _, role in ipairs(roles) do
            local id = pickRole(rng, roleTheme[role], byId, used)
            if not id then
                id = pickRole(rng, def[bucket] or {}, byId, used)
            end
            if not id then
                id = pickRole(rng, def.drive or {}, byId, used)
            end
            if not id then
                local ids = {}
                for cid in pairs(byId) do ids[#ids + 1] = cid end
                table.sort(ids)
                id = pickRole(rng, ids, byId, used)
            end
            if id then
                local entry = byId[id]
                local react = (params.visualReactivity or 1.0)
                            * (ROLE_REACT[role] or 0.9)
                local vis = {
                    shader = entry.id,
                    role = role,
                    audioRole = role,
                    name = (sec.name or sec.id) .. " " .. role .. " · "
                         .. pretty(entry.id),
                    params = makeParams(rng, entry, energy, theme),
                    reactivity = clamp(react, 0.0, 4.0),
                }
                if NEEDS_IMAGE[entry.id] then vis.image = SEED_IMAGE end
                visuals[#visuals + 1] = vis
            end
        end
        plan[i] = visuals
    end
    return plan
end

return M
