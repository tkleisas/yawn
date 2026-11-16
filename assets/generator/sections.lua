local M = {}

M.types = {
    intro = { bars = 4, energy = 0.25 },
    verse = { bars = 8, energy = 0.5 },
    prechorus = { bars = 4, energy = 0.65 },
    chorus = { bars = 8, energy = 0.9 },
    bridge = { bars = 4, energy = 0.55 },
    outro = { bars = 4, energy = 0.2 },
    ["break"] = { bars = 4, energy = 0.4 },
    drop = { bars = 8, energy = 1.0 },
}

M.defaultTemplate = {
    "intro", "verse", "prechorus", "chorus",
    "verse", "chorus", "outro",
}

local function nameOf(id)
    local s = id:sub(1, 1):upper() .. id:sub(2)
    return (s:gsub("_", " "))
end

function M.plan(ids)
    local plan = {}
    for _, id in ipairs(ids) do
        local t = M.types[id] or M.types.verse
        plan[#plan + 1] = {
            id = id, name = nameOf(id), bars = t.bars, energy = t.energy,
        }
    end
    return plan
end

return M
