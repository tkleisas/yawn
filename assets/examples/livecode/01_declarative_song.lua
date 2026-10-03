-- ─────────────────────────────────────────────────────────────────────
-- 01 · Declarative Onset — pure `song` layer demo (docs/live-coding.md §5)
--
-- The whole is the data: tracks, instruments, FX chains and MIDI clips
-- are defined below; on RUN the DAW converges to this description
-- (diff-apply — re-running after rcaching changes is a no-op).
--
-- Try it:
--   1. New project.                        2. Ctrl+L twice (template, then run)
--   3. Press play from the start of the session grid, launch scene 1-3.
--   4. ~ → Code tab shows this file, regenerated from live state.
-- ─────────────────────────────────────────────────────────────────────

song = {
    bpm = 126,
    tracks = {
        { uid = 1, name = "Kick", volume = 1.0,                    -- adopts default track 1 (keeps its type)
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]    = 0,    -- sine thump
              ["Osc1 Level"]   = 1.0,
              ["Osc2 Level"]   = 0.0,
              ["Sub Level"]    = 0.6,
              ["Filter Cutoff"]= 0.18,
              ["Filter Reso"]  = 0.35,
              ["Filter Env"]   = 0.9,
              ["Amp Attack"]   = 0.001,
              ["Amp Decay"]    = 0.18,
          } },
          clips = {
              [1] = { beats = 4, notes = {
                  { 0.0, 0.25, 36, 1.0 },
                  { 1.0, 0.25, 36, 0.72 },
                  { 2.5, 0.25, 36, 0.8 },
                  { 3.5, 0.25, 36, 0.62 },
              } },
          },
        },

        { uid = 2, name = "Hats", volume = 0.55,                  -- adopts default track 2 (keeps its type)
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]    = 4,    -- noise source
              ["Noise Level"]  = 1.0,
              ["Osc1 Level"]   = 0.0,
              ["Filter Cutoff"]= 0.85,
              ["Filter Type"]  = 2,   -- HP
              ["Filter Reso"]  = 0.15,
              ["Amp Attack"]   = 0.001,
              ["Amp Decay"]    = 0.045,
          } },
          fx = { { id = "eq", params = { ["High Gain"] = 0.7 } } },
          clips = {
              [1] = { beats = 4, notes = {
                  { 0.5, 0.06, 42, 0.45 },
                  { 1.5, 0.06, 42, 0.5 },
                  { 2.5, 0.06, 42, 0.45 },
                  { 3.5, 0.06, 42, 0.42 },
                  { 3.75, 0.12, 46, 0.8 },     -- open hat, pushed
              } },
              [2] = { beats = 4, notes = {
                  { 0.5, 0.06, 42, 0.5 },
                  { 1.0, 0.06, 42, 0.4 },
                  { 1.5, 0.06, 42, 0.55 },
                  { 2.0, 0.06, 42, 0.4 },
                  { 2.5, 0.06, 42, 0.5 },
                  { 3.0, 0.06, 42, 0.4 },
                  { 3.5, 0.06, 42, 0.6 },
                  { 3.75, 0.12, 46, 0.85 },
              } },
          },
        },

        { name = "Bass", type = "midi", volume = 0.8,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"]    = 2,    -- saw
              ["Osc1 Level"]   = 0.9,
              ["Sub Level"]    = 0.5,
              ["Filter Cutoff"]= 0.24,
              ["Filter Type"]  = 0,   -- LP
              ["Filter Env"]   = 0.55,
              ["Amp Attack"]   = 0.002,
              ["Amp Decay"]    = 0.5,
          } },
          fx = {
              { id = "compressor", params = { ["Ratio"] = 0.6, ["Threshold"] = 0.45 } },
              { id = "eq", params = { ["Low Gain"] = 0.85 } },
          },
          clips = {
              [1] = { beats = 4, notes = {
                  { 0.0, 0.45, 36, 0.95 },
                  { 1.0, 0.2, 36, 0.6 },
                  { 1.5, 0.45, 38, 0.8 },
                  { 2.5, 0.45, 43, 0.9 },      -- A
                  { 3.5, 0.2, 41, 0.75 },
                  { 3.75, 0.2, 43, 0.7 },
              } },
          },
        },

        { name = "Chords", type = "midi", volume = 0.5,
          instrument = { id = "fmsynth", params = {
              ["Algorithm"]    = 4,
              ["Feedback"]     = 0.1,
              ["Op1 Level"]    = 0.85,
              ["Op1 Attack"]   = 0.02,
              ["Op1 Release"]  = 0.4,
              ["Op2 Level"]    = 0.4,
              ["Op2 Ratio"]    = 2,
          } },
          fx = {
              { id = "chorus", params = { ["Depth"] = 0.4 } },
              { id = "delay", params = { ["Wet/Dry"] = 0.18, ["Feedback"] = 0.3 } },
          },
          clips = {
              [1] = { beats = 4, notes = {
                  { 0.0, 1.8, 53, 0.5 }, { 0.0, 1.8, 57, 0.45 },
                  { 0.0, 1.8, 60, 0.4 }, { 0.0, 1.8, 65, 0.35 },
                  { 2.0, 1.8, 55, 0.5 }, { 2.0, 1.8, 59, 0.45 },
                  { 2.0, 1.8, 62, 0.4 }, { 2.0, 1.8, 67, 0.35 },
              } },
              [2] = { beats = 4, notes = {
                  { 0.0, 1.8, 50, 0.5 }, { 0.0, 1.8, 53, 0.45 },
                  { 0.0, 1.8, 57, 0.4 }, { 0.0, 1.8, 62, 0.35 },
                  { 2.0, 1.8, 48, 0.5 }, { 2.0, 1.8, 52, 0.45 },
                  { 2.0, 1.8, 55, 0.4 }, { 2.0, 1.8, 60, 0.35 },
              } },
          },
        },

        { name = "Lead", type = "midi", volume = 0.62,
          instrument = { id = "karplus", params = { ["Damping"] = 0.45 } },
          fx = { { id = "delay", params = { ["Wet/Dry"] = 0.3, ["Feedback"] = 0.45 } },
                 { id = "plate", params = { ["Wet/Dry"] = 0.2 } } },
          clips = {
              [2] = { beats = 4, notes = {
                  { 0.0, 0.75, 89, 0.6 },
                  { 1.0, 0.5, 84, 0.5 },
                  { 1.5, 0.5, 82, 0.45 },
                  { 2.5, 1.0, 77, 0.55 },
                  { 3.5, 0.5, 80, 0.5 },
              } },
          },
        },
    },
}

yawn.toast("Declarative song applied — scene 1 launch, then scene 2 for the edit.")
