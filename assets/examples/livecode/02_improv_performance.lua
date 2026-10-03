-- ─────────────────────────────────────────────────────────────────────
-- 02 · Improv Column — pure performance-layer demo (docs/live-coding.md §4)
--
-- No `song` block: everything is generated musically in time. Callbacks
-- fire slightly BEFORE their target moment (lookahead) and receive the
-- target beat/bar — pass it through to yawn.note(..., at_beat) so notes
-- stay sample-accurate on the audio thread.
--
-- Try it:
--   1. New project, Ctrl+L twice with this file at ~/.yawn/livecode/main.lua
--   2. Press play (any bar will do — the layer is transport-anchored).
--   3. In the ~ console, run:  yawn.state.density = 0.9   ← evolution
--   4. Ctrl+Shift+L to hard-reload; yawn.state migrates across reloads.
-- ─────────────────────────────────────────────────────────────────────

yawn.set_playing(true)          -- start the transport (the improv layer
                                -- is transport-anchored: no play, no notes)
improv.lookahead(0.15)          -- schedule 150ms ahead of fire time
improv.late_policy("drop")      -- a late iteration is skipped, not played

-- Shared context — rebuilt if missing, survives reloads via yawn.state.
yawn.state = yawn.state or {}
yawn.state.density    = yawn.state.density or 0.7    -- 0..1 hats density
yawn.state.root       = yawn.state.root or 36        -- A1
yawn.state.prog       = yawn.state.prog or { 0, -2, 3, -4 }  -- Am, G, C, F (semitones vs root)

local SCALE = { 0, 3, 5, 7, 10 }           -- A minor pentatonic offsets

-- ── Four-on-the-floor + ghost kick ────────────────────────────────────
improv.every(1, function(beat)
    yawn.note(0, 36, 127, 0.25, 0, beat)
    if beat % 2 ~= 0.5 then
        yawn.note(0, 36, 70, 0.1, 0, beat + 0.75)   -- ghost off the grid
    end
end)

-- ── Hats: 8th grid, density-shaped ────────────────────────────────────
improv.every(0.5, function(beat)
    local open = (beat % 2 == 0.5)
    local quiet  = 0.35 + 0.2 * ((beat % 4) / 4)
    if math.random() < yawn.state.density or open then
        yawn.note(1, open and 46 or 42, open and 92 or
                  math.floor(50 + 40 * math.random()), 0.08, 0, beat)
    end
end)

-- ── Bassline: root-driven, per-bar harmony from the progression ───────
improv.on_bar(function(bar)
    local bpb   = 4.0
    local startBeat = (bar - 1) * bpb          -- on_bar receives 1-based bar
    local chord = yawn.state.prog[((bar - 1) % #yawn.state.prog) + 1]

    for step = 0, 3 do
        local b = startBeat + step
        local pitch = yawn.state.root + chord
        if step == 1 then pitch = pitch + 7 end
        if step == 3 then pitch = pitch + 12 end
        yawn.note(2, pitch, 100, 0.45, 0, b)
        if step == 2 then                        -- syncopation fill
            yawn.note(2, pitch + 5, 75, 0.2, 0, b + 0.5)
        end
    end

    yawn.log(("bar %d — chord %+d, density %.2f")
             :format(bar, chord, yawn.state.density))
end)

-- ── Sparse lead: notes picked from the pentatonic above the chord ─────
improv.every(4, function(beat)
    local n = 3 + math.random(0, 3)
    for i = 0, n - 1 do
        local pitch = yawn.state.root + 48 + SCALE[(i % #SCALE) + 1]
                        + 12 * math.random(0, 1)
        yawn.note(3, pitch, 60 + math.random(30), 0.4, 0,
                  beat + i * 1.2)
    end
end, 4)                                        -- four iterations, then re-arms

-- ── Every 8 bars: evolve — density breathes, kick double ──────────────
improv.on_bar(function(bar)
    if bar % 8 == 0 then
        yawn.state.density = 0.5 + 0.4 * math.random()
        yawn.toast(("evolution @bar %d → density %.2f"):format(bar, yawn.state.density))
    end
end)
