-- ─────────────────────────────────────────────────────────────────────
-- 07 · Oxygen Globe — an homage to Jean-Michel Jarre's Oxygène cover
-- (Michel Granger's Earth-as-egg with a skull inside), built from two
-- glTF models and the oxygen shader pair:
--
--   Skull → 38_oxygen_skull.frag + skull.glb  (bone, ember-lit, rotating)
--   Globe → 39_oxygen_globe.frag + earth.glb  (Earth shell, fissures)
--
-- The `morph` knob on the Globe clip mixes the whole scene from
-- 0 = 100% skull to 1 = 100% Earth (mid-morph is a double exposure with
-- the fissures blazing); `autoMix` lets it breathe on its own. Both
-- models spin via `modelSpinY`; everything is a normal @range knob, so
-- LFOs, MIDI Learn and automation work out of the box.
--
-- Try it: new project → Ctrl+L twice (template, then run). Scene 1
-- self-starts; F11 on the visual output window for fullscreen. Select
-- the Globe track → Visual Params → drag `morph`.
-- ─────────────────────────────────────────────────────────────────────

song = {
    bpm = 92,
    tracks = {
        { uid = 1, name = "Pulse", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.7,
              ["Filter Cutoff"] = 0.16, ["Filter Env"] = 0.9,
              ["Amp Decay"] = 0.35 } },
          clips = { [1] = yawn.drums{ BD = "x-------x-------" } } },
        { uid = 2, name = "Drone", type = "midi", volume = 0.32,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 2, ["Osc1 Level"] = 0.55,
              ["Osc2 Wave"] = 1, ["Osc2 Level"] = 0.35,
              ["Osc2 Detune"] = 0.12, ["Sub Level"] = 0.45,
              ["Filter Cutoff"] = 0.20, ["Filter Reso"] = 0.15,
              ["Filter Env"] = 0.30, ["Amp Attack"] = 0.6,
              ["Amp Sustain"] = 0.8, ["Amp Release"] = 2.2 } },
          fx = { { id = "chorus", params = { ["Depth"] = 0.35 } },
                 { id = "plate", params = { ["Wet/Dry"] = 0.30 } } },
          clips = { [1] = yawn.midi("[A1C2E2]_1*4@0.45") } },
        { name = "Skull", type = "visual" },
        { name = "Globe", type = "visual" },
    },
}

-- The skull sits underneath; the globe crossfades over it.
yawn.set_visual("Skull", 1, "38_oxygen_skull.frag", {
    name = "oxygen skull", model = "skull.glb",
    params = { modelScale = 0.72, modelSpinY = 5.0, modelPosY = 0.02,
               ember = 0.85, sky = 0.70, palette = 0.40 },
})
yawn.set_visual("Globe", 1, "39_oxygen_globe.frag", {
    name = "oxygen globe", model = "earth.glb",
    params = { modelScale = 0.95, modelSpinY = 3.5, modelRotX = -14,
               morph = 1.0, autoMix = 0.28, crack = 0.55, atmo = 0.80,
               ember = 0.85, palette = 0.35 },
})

yawn.launch_scene(1)      -- planet + pulse, on the downbeat
