-- ─────────────────────────────────────────────────────────────────────
-- 08 · Skull Ring — the Oxygène homage grows a carousel: the Earth
-- (model clip, 39_oxygen_globe.frag) below, and above it a ring of
-- twelve skulls orbiting a slow central skull, drawn by the scene
-- script assets/examples/scripts/skull_ring.lua and styled by
-- 38_oxygen_skull.frag in overlay mode.
--
-- The globe still morphs to the lone skull with its `morph` knob; the
-- ring adds the ritual. Both clips are driven by the same slow pulse.
--
-- Try it: new project → Ctrl+L twice. Scene 1 self-starts. Select the
-- Orbit track → Visual Params (`ember`, `sky`); the scene script reads
-- ctx.audio.kick, so the ring breathes on its own.
-- ─────────────────────────────────────────────────────────────────────

song = {
    bpm = 90,
    tracks = {
        { uid = 1, name = "Pulse", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.7,
              ["Filter Cutoff"] = 0.16, ["Filter Env"] = 0.9,
              ["Amp Decay"] = 0.4 } },
          clips = { [1] = yawn.drums{ BD = "x-------x---x---" } } },
        { uid = 2, name = "Drone", type = "midi", volume = 0.30,
          instrument = { id = "subsynth", params = {
              ["Osc1 Wave"] = 2, ["Osc1 Level"] = 0.5,
              ["Sub Level"] = 0.5, ["Filter Cutoff"] = 0.18,
              ["Amp Attack"] = 0.4, ["Amp Sustain"] = 0.8,
              ["Amp Release"] = 2.0 } },
          fx = { { id = "plate", params = { ["Wet/Dry"] = 0.35 } } },
          clips = { [1] = yawn.midi("A1_1 C2_1 F1_1 G1_1") } },
        { name = "Globe", type = "visual" },
        { name = "Orbit", type = "visual" },
    },
}

-- The Earth shell underneath (morphing toward the skull, slowly).
yawn.set_visual("Globe", 1, "39_oxygen_globe.frag", {
    name = "oxygen globe", model = "earth.glb",
    params = { modelScale = 0.95, modelSpinY = 3.0, modelRotX = -14,
               morph = 1.0, autoMix = 0.22, crack = 0.50, atmo = 0.80 },
})

-- The ring above: 12 orbiting skulls + a central one, alpha overlay.
yawn.set_visual("Orbit", 1, "38_oxygen_skull.frag", {
    name = "skull ring", model = "skull.glb", scene = "skull_ring.lua",
    params = { overlay = 1.0, ember = 0.90, sky = 0.55, kick = 0.55 },
})

yawn.launch_scene(1)
