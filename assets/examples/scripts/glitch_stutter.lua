-- Glitch stutter — datamosh in 3D. The model snaps to quantised "wrong"
-- positions and rotations on the beat, holds (freezes) between snaps, and
-- takes a big displaced jump on the kick. A fixed camera keeps the frame
-- stable so the stutter reads as motion, not a camera chase.
--
-- Drop on any model clip, then Set Scene Script… → this file.

-- Deterministic per-step pseudo-random in [0,1). Lua's % returns the sign
-- of the divisor (positive), so this stays non-negative.
local function hash(n)
    return (math.sin(n * 91.7) * 43758.5453) % 1.0
end

function tick(ctx)
    -- Quantise the beat into ~8 steps; the model holds each step (freeze)
    -- and only jumps when the step changes — steppy, not smooth.
    local stepN = math.floor(ctx.beat * 8.0)
    local r1 = hash(stepN)
    local r2 = hash(stepN + 17.0)
    local r3 = hash(stepN + 53.0)

    -- Only some steps glitch; the rest sit near origin (the "locked" frame).
    local glitching = r1 > 0.55
    local jump = ctx.audio.kick                 -- 0..1, decays after each hit
    local mag  = (glitching and 0.5 or 0.0) + jump * 1.2

    local pos = { (r1 - 0.5) * mag,
                  (r2 - 0.5) * mag,
                  (r3 - 0.5) * mag * 0.5 }

    -- Snap rotation to coarse increments for a corrupted-frame look, with a
    -- slow continuous drift so a locked frame still feels alive.
    local rot = { math.floor(r1 * 4.0) * 90.0,
                  math.floor(r2 * 8.0) * 45.0 + ctx.beat * 5.0,
                  math.floor(r3 * 4.0) * 90.0 }

    -- Kick pops the scale; occasional flat squash on a glitch step.
    local s = 1.0 + jump * 0.5
    if glitching and r2 > 0.7 then s = s * 0.6 end

    return {
        {
            position = pos,
            rotation = rot,
            scale    = s,
            emissive = jump * 1.5,                          -- glow on the kick
            color    = { 1.0, 1.0 - jump * 0.5, 1.0 - jump }, -- redshift on hit
        }
    }, {
        camera = { pos = { 0, 0, 4.0 }, target = { 0, 0, 0 }, fov = 50 }
    }
end
