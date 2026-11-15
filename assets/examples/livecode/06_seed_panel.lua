-- ─────────────────────────────────────────────────────────────────────
-- 06 · Seed Panel — the seed-picture visual family, pre-patched
-- (shaders 32–36; see docs/visual.md → bundled shader pack).
--
-- Four visual layers hang off ONE seed picture (seed_emblem.png), so
-- palette and structure stay coherent:
--   Mandala → 32_mandala_seed.frag   the icon (gold lotus-wheel)
--   Frame   → 33_thangka_frame.frag  brocade + vermilion border
--   Eyes    → 34_eye_field.frag      watching-eye garland
--   Ink     → 36_ink_brush.frag      enso + vermilion chop
-- A slow groove drives the reactivity: the kick blooms the mandala's
-- gold and blinks the eyes; the low band thickens the ink bleed. All
-- layers preset the traditional palette (`palette = 1`).
--
-- Try it: new project → Ctrl+L twice (template, then run). Scene 1
-- self-starts and the visual output window opens on launch (F11 there
-- = fullscreen). Select a visual track and drag the Visual Params
-- knobs to taste, or right-click any clip → Set Image… to swap the
-- seed (the whole panel re-skins from it).
-- ─────────────────────────────────────────────────────────────────────

song = {
    bpm = 92,
    tracks = {
        { uid = 1, name = "Kick", type = "midi",
          instrument = { id = "subsynth", params = {
              ["Osc1 Level"] = 1.0, ["Sub Level"] = 0.6,
              ["Filter Cutoff"] = 0.18, ["Filter Env"] = 0.9,
              ["Amp Decay"] = 0.2 } },
          clips = { [1] = yawn.drums{ BD = "x---x---x-x-x---" } } },
        { uid = 2, name = "Drone", type = "midi", volume = 0.30,
          instrument = { id = "karplus", params = { ["Damping"] = 0.55 } },
          fx = { { id = "plate", params = { ["Wet/Dry"] = 0.35 } } },
          clips = { [1] = yawn.midi("A1_1 C2_1 F1_1 G1_1") } },
        { name = "Mandala", type = "visual" },
        { name = "Frame", type = "visual" },
        { name = "Eyes", type = "visual", volume = 0.95 },
        { name = "Ink", type = "visual", volume = 0.90 },
    },
}

-- One seed for the whole family. Bare image names resolve against the
-- bundled examples dir (assets/examples/livecode); clip params preset
-- the shaders' @range knobs.
local seed = "seed_emblem.png"
yawn.set_visual(2, 1, "32_mandala_seed.frag", {
    name = "mandala", image = seed,
    params = { palette = 1.0, zoom = 0.78, segments = 12,
               petals = 12, rings = 3 },
})
yawn.set_visual(3, 1, "33_thangka_frame.frag", {
    name = "thangka", image = seed,
    params = { palette = 1.0, band = 0.10 },
})
yawn.set_visual(4, 1, "34_eye_field.frag", {
    name = "eyes", image = seed,
    params = { palette = 1.0, eyes = 9, rings = 2 },
})
yawn.set_visual(5, 1, "36_ink_brush.frag", {
    name = "ink", image = seed,
    params = { palette = 1.0, enso = 0.34, splatter = 0.35 },
})

yawn.launch_scene(1)      -- sound + panel, on the downbeat
