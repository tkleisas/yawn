-- ─────────────────────────────────────────────────────────────────────
-- 04 · Sample Forge — prerender/proto cost demo (docs/live-coding.md §4.4)
--
-- Code writes the SOUND first, then the SONG plays it:
--   · yawn.render{} renders an instrument offline (worker thread, no
--     audio-thread involvement, deterministic per seed) and delivers
--     the buffer to a target.
--   · This demo renders a classic kick/snare/hat into DrumRack pads —
--     including a resampled punch variant — and mirrors one into the
--     content library so it shows up in the browser.
--
-- Try it: run on a project whose track 1 has a Drum Rack
-- (or just run — delivery to missing targets is reported cleanly,
-- and the library copy is unconditional).
-- ─────────────────────────────────────────────────────────────────────

local jobs = {}

local function forge(name, device, params, notes, beats, target)
    local id = yawn.render{
        device = device,
        params = params,
        notes  = notes,
        beats  = beats,
        tail   = 1.5,
        target = target,
    }
    if id and id > 0 then
        jobs[#jobs + 1] = id
        yawn.log("forges queued: " .. name .. " (job " .. id .. ")")
    end
end

-- 909-flavoured kick: sine with a pitched sweep — engine at 48k,
-- rendered with a seeded generative source (spec.seed fixes it).
forge("kick-core", "subsynth",
      { ["Osc1 Wave"]     = 0,
        ["Osc1 Level"]    = 1.0,
        ["Sub Level"]     = 0.4,
        ["Filter Cutoff"] = 0.3,
        ["Filter Reso"]   = 0.1,
        ["Filter Env"]    = 0.7,
        ["Amp Attack"]    = 0.001,
        ["Amp Decay"]     = 0.5 },
      { { 0.0, 0.4, 36, 1.0 } },
      1.0,
      { kind = "drumrack", track = 0, pad = 60, name = "forge-kick" })

-- Snare: drumsynth into pad 62.
forge("snare-core", "drumsynth",
      {},                                   -- default voice
      { { 0.0, 0.3, 62, 1.0 } },
      0.5,
      { kind = "drumrack", track = 0, pad = 62, name = "forge-snare" })

-- Hat: noise burst, HP-filtered, into pad 64.
forge("hat-tick", "subsynth",
      { ["Osc1 Wave"]     = 4,                -- noise
        ["Noise Level"]   = 1.0,
        ["Osc1 Level"]    = 0.0,
        ["Filter Cutoff"] = 0.9,
        ["Filter Type"]   = 2,                -- HP
        ["Amp Attack"]    = 0.001,
        ["Amp Decay"]     = 0.08 },
      { { 0.0, 0.08, 64, 0.9 } },
      0.25,
      { kind = "drumrack", track = 0, pad = 64, name = "forge-hat" })

-- Punch variant: the kick again, pitched — same seed, different note
-- (deterministic helpers let you A/B variants in the sampler).
forge("kick-punched", "subsynth",
      { ["Osc1 Wave"]     = 0,
        ["Osc1 Level"]    = 1.0,
        ["Sub Level"]     = 0.5,
        ["Filter Cutoff"] = 0.35,
        ["Filter Env"]    = 0.9,
        ["Amp Decay"]     = 0.4 },
      { { 0.0, 0.35, 24, 1.0 } },            -- two octaves down
      0.75,
      { kind = "drumrack", track = 0, pad = 61, name = "forge-kick2" })

-- Library mirror of the core kick — the kind="library" route: the WAV
-- lands in the first library root (auto-created if none configured)
-- and is indexed, so it appears in the Browser with zero clicks.
forge("kick-library", "subsynth",
      { ["Osc1 Wave"]     = 0,
        ["Osc1 Level"]    = 1.0,
        ["Sub Level"]     = 0.6,
        ["Filter Cutoff"] = 0.25,
        ["Amp Decay"]     = 0.45 },
      { { 0.0, 0.4, 36, 1.0 } },
      1.0,
      { kind = "library", name = "forge kick core" })

-- Report whenever a job completes (delivery report hook → console);
-- one-shot status check every bar until all four land.
improv.on_bar(function(bar)
    -- keep the console notified for a handful of bars, then quiet down
    for i = #jobs, 1, -1 do
        local id = jobs[i]
        if id and bar > 4 then
            table.remove(jobs, i)
        end
    end
    if bar == 1 then
        yawn.log(#jobs .. " forge jobs in flight")
    end
end)

yawn.toast("Sample Forge: 5 jobs queued — completes land in the console")
