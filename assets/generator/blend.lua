local M = {}

function M.load(rng, mixes)
    local entries, total = {}, 0
    for _, m in ipairs(mixes or {}) do
        local id = m.id
        local ok, style = pcall(require, "styles." .. tostring(id))
        if not ok then
            error("unknown style '" .. tostring(id) .. "': " .. tostring(style))
        end
        local w = tonumber(m.weight) or 1
        if w < 0 then w = 0 end
        entries[#entries + 1] = { id = id, weight = w, style = style }
        total = total + w
    end
    if total <= 0 then
        entries = { { id = "pop", weight = 1, style = require("styles.pop") } }
        total = 1
    end
    for _, e in ipairs(entries) do e.norm = e.weight / total end
    return entries, total
end

function M.pick(rng, entries)
    local r = rng.float()
    local acc = 0
    for _, e in ipairs(entries) do
        acc = acc + e.norm
        if r <= acc + 1e-12 then return e end
    end
    return entries[#entries]
end

function M.mean(entries, key, fallback)
    local sum, wsum = 0, 0
    for _, e in ipairs(entries) do
        local v = e.style[key]
        if type(v) == "table" then v = (v[1] + v[2]) * 0.5 end
        if type(v) == "number" then
            sum = sum + v * e.norm
            wsum = wsum + e.norm
        end
    end
    if wsum <= 1e-12 then return fallback end
    return sum / wsum
end

function M.range(style, key, rng, fallback)
    local v = style[key]
    if type(v) == "table" then
        return v[1] + (v[2] - v[1]) * rng.float()
    end
    if type(v) == "number" then return v end
    return fallback
end

return M
