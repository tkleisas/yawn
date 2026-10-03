-- ─────────────────────────────────────────────────────────────────────
-- 03 · song + improv — the two-layer convention (docs/live-coding.md §1.1)
--
-- One file, two concerns:
--   · `song`   — the durable project definition (declarative, re-run diffed)
--   · `improv` — the ephemeral performance layer (schedule tickets, fenced
--                by generation — a failed re-run keeps the old layer live)
--
-- This is the template shape the live-coding stage is built around.
-- ─────────────────────────────────────────────────────────────────────

-- ═══ Layer 1: song ═══════════════════════════════════════════════════
song = {
    bpm = 124,
    tracks = {
        { uid = 1, name = "Kick", volume = 1.0,                    -- adopts default track 1 (keeps its type)
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]     = 0,
              ["Osc1 Level"]    = 1.0,
              ["Sub Level"]     = 0.7,
              ["Filter Cutoff"] = 0.2,
              ["Filter Env"]    = 0.95,
              ["Amp Decay"]     = 0.2,
          } },
        },
        { uid = 2, name = "Clap", volume = 0.6,                    -- adopts default track 2 (keeps its type)
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]     = 4,       -- noise burst body
              ["Noise Level"]   = 1.0,
              ["Osc1 Level"]    = 0.0,
              ["Filter Cutoff"] = 0.6,
              ["Filter Type"]   = 1,       -- BP: band around the clap
              ["Filter Reso"]   = 0.4,
              ["Amp Attack"]    = 0.001,
              ["Amp Decay"]     = 0.09,
          } },
          fx = { { id = "reverb", params = { ["Wet/Dry"] = 0.25 } } },
        },
        { name = "Bass", type = "midi", volume = 0.82,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]     = 1,       -- tri — rounder
              ["Sub Level"]     = 0.6,
              ["Filter Cutoff"] = 0.3,
              ["Filter Env"]    = 0.4,
              ["Amp Decay"]     = 0.6,
          } },
          fx = { { id = "compressor", params = { ["Threshold"] = 0.4 } } },
        },
        { name = "Keys", type = "midi", volume = 0.5,
          instrument = { id = "wavetable", params = {
              ["Position"]   = 0.7,
              ["Filter Cut"] = 0.55,
              ["Amp Dec"]    = 0.4,
          } },
          fx = {
              { id = "filter", params = { ["Cutoff"] = 0.6 } },
              { id = "delay",  params = { ["Wet/Dry"] = 0.22, ["Feedback"] = 0.35 } },
          },
        },
    },
}

-- ═══ Layer 2: improv ═════════════════════════════════════════════════
improv.lookahead(0.12)

-- Kick: the classic — one on every beat, robotic.
improv.every(1, function(beat)
    yawn.note(0, 36, 120, 0.3, 0, beat)
end)

-- Clap: backbeat only, with a probabilistic push on bar 4.
improv.on_bar(function(bar)
    local bpb = 4.0
    local base = (bar - 1) * bpb + 1.0
    yawn.note(1, 38, 100, 0.15, 0, base)
    if bar % 4 == 3 and math.random() < 0.8 then
        yawn.note(1, 38, 82, 0.15, 0, base + 2.5)   -- off-grid clap
    end
end)

-- Bass: rolling root-fifth under the harmony of the moment.
local roots = { 36, 31, 33, 29 }        -- A, G, A♭, F
improv.on_bar(function(bar)
    local root = roots[((bar - 1) % #roots) + 1]
    local bpb = 4.0
    local off = (bar - 1) * bpb
    local last = 0.0
    for i = 1, 6 do
        local b = off + 0.5 * i + (i % 2) * 0.25
        last = b
        yawn.note(2, root + (i % 4 == 0 and 7 or 0), 95, 0.3, 0, b)
    end
    yawn.log("bass pattern set through beat " .. last)
end)

-- Keys: chords stab every other bar, lush voicing, voiced from state.
yawn.state = yawn.state or { voicing = 0 }
improv.every(2, function(beat)
    local v = yawn.state.voicing or 0
    local base = 60 + (v * 5)
    for _, note in ipairs({ 0, 4, 7, 11 }) do
        yawn.note(3, base + note, 55, 1.6, 0, beat)
    end
end)

-- Interaction hook: bump the voicing from the `~` console at any time:
--   yawn.state.voicing = 2   → next chord stabs sit higher.
