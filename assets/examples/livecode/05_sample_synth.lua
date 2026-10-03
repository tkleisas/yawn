-- ─────────────────────────────────────────────────────────────────────
-- 05 · Sample Synth Lab — programmatic sample forging (docs §4.5)
--
-- The script writes the SOUNDS themselves, sample by sample:
--   · additive sine-bank pad (harmonic series, per-voice envelopes)
--   · spectral shaping: FFT the mix, sculpt bins, IFFT back
--   · polyBLEP-corrected "zap" (naive saw + discontinuity correction)
--   · shaped-noise percussion (exponential decay)
-- Every forge lands in the same handle registry the file loaders use,
-- so `yawn.load_sample` plays them straight out of the Lua state.
--
-- Try it: new project → Ctrl+L (twice). Scene 1 self-starts; four
-- forged one-shots cycle through the kit built from nothing but math.
-- ─────────────────────────────────────────────────────────────────────

-- ═══ Layer 1: song — the four sample slots ═══════════════════════════
song = {
    bpm = 100,
    tracks = {
        { uid = 1, name = "Pad", volume = 0.9,
          instrument = { id = "sampler", params = {
              ["Root Note"] = 0.25,         -- C3 = 48/192
              ["Attack"] = 0.0, ["Decay"] = 0.2,
          } } },
        { uid = 2, name = "Shaped", volume = 0.8,
          instrument = { id = "granular", params = {
              ["Grain Size"] = 0.3, ["Position"] = 0.0,
          } } },
        { uid = 3, name = "Zap", volume = 0.75,
          instrument = { id = "sampler" } },
        { uid = 4, name = "Hat", volume = 0.6,
          instrument = { id = "sampler" } },
    },
}

-- ═══ Layer 2: forging ════════════════════════════════════════════════
local sr = yawn.buffer_info(yawn.new_buffer{ frames = 1 }).sr
local twoPi = 2 * math.pi

-- ── Forge 1: additive sine-bank pad (8 s, stereo, detuned voices) ────
local HARMONICS = 12
local BASE = 110.0                       -- A2
local PAD = yawn.new_buffer{
    frames = sr * 8, channels = 2,
    fill = function(f, ch)
        local t = (f - 1) / sr
        local acc = 0.0
        for k = 1, HARMONICS do       -- rolling up: louder in the body
            local amp = 1.0 / (k * k) * (ch == 1 and 1.0 or 0.85)
            acc = acc + amp * math.sin(twoPi * (BASE * k) * (ch == 1 and 1.0 or 1.002) * t)
        end
        local env = math.exp(-t * 0.35) * (1.0 - math.exp(-t * 40.0))
        return 0.28 * env * acc
    end,
}
yawn.buffer_normalize(PAD, 0.85)
yawn.toast("forged additive pad — " .. sr * 8 .. " frames")

-- ── Forge 2: spectral shaping — low-pass the pad via FFT → bin mask ──
do
    local mono = yawn.buffer_mixdown(PAD)
    local d = yawn.buffer_data(mono)
    local raw = d.data[1]
    -- FFT needs a power-of-two length: take the first 4-second window.
    local n = 65536
    if n > #raw then n = 1 while n * 2 <= #raw do n = n * 2 end end
    local x = {}
    for i = 1, n do x[i] = raw[i] end
    local spec = yawn.fft(x)
    -- Bin mask: 1.0 below the knee, tumbling 24 dB/oct above it.
    local knee = math.floor(n / 8)
    local shaped = {}
    for k = 1, n do
        local re, im = spec[2 * k - 1] or 0, spec[2 * k] or 0
        local gain = 1.0
        if k > knee then
            gain = 0.5 ^ ((k - knee) / math.max(1, math.floor(n / 24)))
        end
        shaped[2 * k - 1] = re * gain
        shaped[2 * k]     = im * gain
    end
    local back = yawn.ifft(shaped)
    local shapedBuf = yawn.new_buffer{
        frames = n, channels = 1, data = { back } }
    yawn.buffer_normalize(shapedBuf, 0.8)
    SHAPED = shapedBuf                 -- → "Shaped" granular track
end

-- ── Forge 3: polyBLEP saw "zap" (0.5 s, alias-free) ──────────────
do
    local f0 = 440.0
    local dt = f0 / sr
    local ZAPFRAMES = math.floor(sr * 0.5)
    local phase = 0.0
    local sawSample = function()        -- naive + polyBLEP correction
        local naive = 2.0 * phase - 1.0
        local corr = yawn.polyblep(phase, dt)
        local out = naive + corr
        phase = phase + dt
        if phase >= 1.0 then phase = phase - 1.0 end
        return out
    end
    local ZAP = yawn.new_buffer{
        frames = ZAPFRAMES, channels = 1,
        fill = function(f)
            local t = (f - 1) / sr
            local env = math.exp(-t * 18.0)
            return 0.7 * env * sawSample()
        end,
    }
    yawn.buffer_normalize(ZAP, 0.9)
    yawn.buffer_fade(ZAP, 8, 512)
    ZAPBUF = ZAP                        -- → "Zap" track (ch 1)
end

-- ── Forge 4: shaped-noise hat (0.12 s) ───────────────────────────────
do
    local NF = math.floor(sr * 0.12)
    local HAT = yawn.new_buffer{
        frames = NF, channels = 1,
        fill = function(f)
            local t = (f - 1) / sr
            local env = math.exp(-t * 90.0)
            return 0.8 * env * (math.random() * 2.0 - 1.0)
        end,
    }
    yawn.buffer_normalize(HAT, 0.7)
    HATBUF = HAT                        -- → "Hat" track (ch 1)
end

-- ── Install the forge ──
-- Sustained forges land as SESSION CLIPS (waveforms in the grid, they
-- play on launch); one-shot forges as instrument material for improv.
yawn.set_clip(0, 1, PAD, "forged additive pad")        -- 8 s pad loop
yawn.set_clip(1, 1, SHAPED or PAD, "spectrally shaped") -- 65536 texture
yawn.load_sample(2, ZAPBUF or PAD, "sampler")
yawn.load_sample(3, HATBUF or PAD, "sampler")
yawn.log("forged 4 samples — sr " .. sr .. ", kit loaded")

-- ═══ Layer 3: improv — play the forge ════════════════════════════════
improv.lookahead(0.12)
improv.late_policy("drop")

yawn.state = yawn.state or { root = 36 }
local root = yawn.state.root

-- (Pad + Shaped play straight from their forged clips on scene launch;
--  the improv layer performs the one-shot forges over them.)

-- Zap lead: sparse replies with random target pitch (sampler pitch = note).
improv.every(1, function(beat)
    if math.random() < 0.35 then
        yawn.note(2, root + 12 + math.floor(12 * math.random()), 95, 0.4, 0, beat)
    end
end)

-- Hat ticks on the 8ths (fixed pitch — the sample carries the timbre).
improv.every(0.5, function(beat)
    yawn.note(3, root, beat % 8 == 0.5 and 110 or 70, 0.05, 0, beat)
end)

-- Self-start (quantize resolves at fire time — stopped → downbeat now).
yawn.launch_scene(1)
