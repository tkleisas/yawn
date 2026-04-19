-- Minimal scene script: draws the clip's model once, at the origin.
-- Equivalent to the default (no-script) path. Useful as a starting
-- template — copy and edit in place; YAWN hot-reloads on save.

function tick(ctx)
    return {
        { position = {0, 0, 0},
          rotation = {0, ctx.time * 30, 0},   -- slow Y-spin, 30°/s
          scale    = 1.0 }
    }
end
