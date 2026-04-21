-- Ableton Move controller script for YAWN
--
-- Verified on hardware:
--   • Pads send velocity-sensitive notes 68..99 (bottom-left → top-right)
--   • Pads accept NoteOn(pad, color) back on channel 0 to light LEDs
--     (Push convention), but Move's firmware decays the LED state unless
--     we re-assert it — so we run a 1 Hz heartbeat in on_tick
-- Best-guess / TODO:
--   • 8 encoders assumed CC 71..78 ch 0 (Push convention)
--   • Transport CCs are Push 1 guesses — real values unknown
--   • Color palette indices below are best guesses — iterate as we map
--     Move's actual palette
--
-- When a CC/note isn't recognised, we log it once (throttled) so you can
-- watch the YAWN log and quickly build up the mapping.

local scales = require("scales")
local pads   = require("pads")

-- ── Move MIDI map ──────────────────────────────────────────────────────────
--
-- Everything below is observed on real hardware (Move → YAWN monitor panel).
-- All controls on MIDI channel 1 — our on_midi handler ignores channel
-- for now, so this doesn't matter for dispatch.

-- Encoders: relative, Ableton convention (Push uses the same range).
-- TODO: verify on Move — spin each encoder and confirm CCs match.
local ENCODER_CCS = { 71, 72, 73, 74, 75, 76, 77, 78 }
local ENCODER_SENSITIVITY = 0.05

-- Transport
local CC_PLAY       = 85
local CC_RECORD     = 86
local CC_SHIFT      = 49

-- Octave / navigation
local CC_OCTAVE_UP   = 55   -- +
local CC_OCTAVE_DOWN = 54   -- −
local CC_TRACK_LEFT  = 62   -- <
local CC_TRACK_RIGHT = 63   -- >

-- Track select buttons (CC numbers are inverted vs. visual order:
-- track1 is the highest CC, track4 the lowest)
local CC_TRACK_1 = 43
local CC_TRACK_2 = 42
local CC_TRACK_3 = 41
local CC_TRACK_4 = 40

-- Mode toggles
--   • plain:      flip pad submode (scale ↔ chromatic)
--   • with Shift: cycle pad row-interval preset (4ths → 3rds → 5ths → Octaves)
local CC_MODE_TOGGLE = 50   -- Track / Session

-- Row-interval presets. Each row of pads is this many semitones above
-- the row below it; the column steps are scale-degree in scale mode or
-- chromatic semitones in chromatic mode.
local ROW_INTERVAL_PRESETS = {
    {name = "4ths",    semis = 5},
    {name = "3rds",    semis = 4},
    {name = "5ths",    semis = 7},
    {name = "Octaves", semis = 12},
}

-- Main (top-left, DETENTED) encoder — the selection / navigation knob.
-- Relative two's-complement 7-bit on CC 14; touch on Note A1 (57).
--   • plain turn:  selected track ±1 (cycles project)
--   • Shift+turn:  scale index ±1 (cycles scales.catalog)
--   • touch:       toast the current scale/root, no change
local CC_MAIN_ENCODER         = 14
local NOTE_MAIN_ENCODER_TOUCH = 57   -- A1

-- Master (smooth, right side) encoder — the continuous value knob.
-- Relative two's-complement 7-bit on CC 79; touch on Note G#1 (56).
--   • plain turn:  master volume (0..1)
--   • Shift+turn:  project BPM
--   • touch:       toast the current value, no change
local CC_MASTER_ENCODER         = 79
local NOTE_MASTER_ENCODER_TOUCH = 56   -- G#1
local MASTER_VOL_SENSITIVITY    = 0.01   -- 0..1 per tick → 100 ticks full sweep
local MASTER_BPM_SENSITIVITY    = 0.5    -- BPM per tick (Shift engages fine mode ×0.1)

-- Scene-launch buttons — the 16 numbered buttons at the top of Move.
-- Button 1 = note 28 (E0), Button 16 = note 43 (G1). We use the first
-- 8 as scene launches; buttons 9-16 are logged for discovery but not
-- yet mapped (no obvious semantic on a 4-scene grid).
local NOTE_BUTTON_BASE = 28
local NOTE_BUTTON_END  = 43

-- Unmapped but discovered — logged so we don't spam "unknown CC" for
-- these. Wire them once we extend the Lua API (undo, mute, etc.).
local KNOWN_UNWIRED_CCS = {
    [56]  = "Undo",
    [58]  = "Loop",
    [52]  = "Capture",
    [118] = "Sample",
    [88]  = "Mute",
    [119] = "X",
    [60]  = "Copy",
    [51]  = "< Back",
}

-- ── State ──────────────────────────────────────────────────────────────────

local shift_held  = false
local param_page  = 0    -- 8 params per page
local last_track  = -1

-- Throttled logging for unknown CCs
local unknown_cc_seen = {}

local NOTE_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}

-- Status toast helpers — Move has no screen, so every state change
-- surfaces in YAWN's top-center toast banner.
local function toast_scale()
    local s = scales.catalog[pads.scale_index]
    if not s then return end
    yawn.toast(NOTE_NAMES[pads.root_note + 1] .. " " .. s.name)
end

local function toast_octave()
    yawn.toast("Octave: " .. tostring(pads.octave))
end

local function toast_root()
    toast_scale()
end

local function toast_track()
    local t = yawn.get_selected_track()
    local name = yawn.get_track_name(t)
    if name and name ~= "" then
        yawn.toast(string.format("Track %d: %s", t + 1, name))
    else
        yawn.toast("Track " .. tostring(t + 1))
    end
end

local function toast_submode()
    yawn.toast((pads.submode == pads.SUBMODE_CHROMATIC)
               and "Chromatic mode" or "Scale mode")
end

-- Find which preset (if any) the current row_interval matches. Falls
-- back to 1 (4ths) when the user set something custom elsewhere.
local function current_layout_index()
    for i, p in ipairs(ROW_INTERVAL_PRESETS) do
        if p.semis == pads.row_interval then return i end
    end
    return 1
end

-- Cycle to the next layout preset in the forward direction, wrapping.
local function cycle_layout_preset()
    local idx = current_layout_index()
    idx = (idx % #ROW_INTERVAL_PRESETS) + 1
    local p = ROW_INTERVAL_PRESETS[idx]
    pads.set_row_interval(scales, p.semis)
    refresh_pad_leds()
    yawn.toast("Layout: " .. p.name)
end

-- ── Helpers ────────────────────────────────────────────────────────────────

local function log_once(tag, msg)
    if not unknown_cc_seen[tag] then
        unknown_cc_seen[tag] = true
        yawn.log(msg)
    end
end

local function current_param_count()
    return yawn.get_device_param_count("instrument", 0) or 0
end

local function clamped_page()
    local n = current_param_count()
    local max_page = math.max(0, math.ceil(n / 8) - 1)
    if param_page > max_page then param_page = max_page end
    return param_page, max_page
end

-- Decode the relative two's-complement 7-bit encoder value into a signed
-- delta. Values 1..63 are +1..+63 (clockwise); 65..127 are -63..-1
-- (counter-clockwise). 64 means "idle".
local function decode_relative(value)
    if value > 63 then return value - 128 end
    return value
end

-- Toast helpers for master encoder (keep format consistent with peek path).
local function toast_master_volume()
    local v = yawn.get_master_volume() or 1.0
    yawn.toast(string.format("Volume: %d%%", math.floor(v * 100 + 0.5)))
end

local function toast_bpm()
    yawn.toast(string.format("BPM: %.1f", yawn.get_bpm() or 120))
end

-- Master (smooth) encoder: plain = volume, Shift = BPM.
-- Smooth so fractional values feel natural — no detent discretization.
local function apply_master_encoder(value)
    local delta = decode_relative(value)
    if delta == 0 then return end
    if shift_held then
        local bpm = yawn.get_bpm() or 120
        local new_bpm = bpm + delta * MASTER_BPM_SENSITIVITY
        if new_bpm < 20 then new_bpm = 20 end
        if new_bpm > 999 then new_bpm = 999 end
        yawn.set_bpm(new_bpm)
        toast_bpm()
    else
        local v = yawn.get_master_volume() or 1.0
        local new_v = v + delta * MASTER_VOL_SENSITIVITY
        if new_v < 0.0 then new_v = 0.0 end
        if new_v > 1.0 then new_v = 1.0 end
        yawn.set_master_volume(new_v)
        toast_master_volume()
    end
end

-- Main (dented) encoder: plain = track select, Shift = scale select.
-- One notch of the detent = one unit of change, so navigation feels
-- tactile and unambiguous — no runaway on a hard spin. Each change
-- toasts so the user always sees where they are.
local function apply_main_encoder(value)
    local delta = decode_relative(value)
    if delta == 0 then return end
    if shift_held then
        local n = #scales.catalog
        pads.scale_index = ((pads.scale_index - 1 + delta + n) % n) + 1
        pads.recompute(scales)
        refresh_pad_leds()
        toast_scale()
    else
        local n = yawn.get_track_count()
        if n > 0 then
            local t = yawn.get_selected_track()
            yawn.set_selected_track((t + delta + n) % n)
            toast_track()
        end
    end
end

-- Apply a relative encoder delta (two's-complement 7-bit: values > 63 are
-- negative). Normalized parameter increments.
local function apply_encoder(cc, value)
    local page, _ = clamped_page()
    local encoder_idx = -1
    for i, e in ipairs(ENCODER_CCS) do
        if e == cc then encoder_idx = i - 1; break end
    end
    if encoder_idx < 0 then return false end

    local param_idx = page * 8 + encoder_idx
    if param_idx >= current_param_count() then return true end

    local delta_raw = value
    if delta_raw > 63 then delta_raw = delta_raw - 128 end
    local delta = delta_raw * ENCODER_SENSITIVITY
    if shift_held then delta = delta * 0.1 end  -- fine mode

    local minv = yawn.get_device_param_min("instrument", 0, param_idx) or 0
    local maxv = yawn.get_device_param_max("instrument", 0, param_idx) or 1
    local curr = yawn.get_device_param_value("instrument", 0, param_idx) or 0
    local range = maxv - minv
    if range <= 0 then return true end
    local new_val = curr + delta * range
    if new_val < minv then new_val = minv end
    if new_val > maxv then new_val = maxv end
    yawn.set_device_param("instrument", 0, param_idx, new_val)
    return true
end

-- ── Pad LEDs ───────────────────────────────────────────────────────────────
--
-- Move accepts NoteOn(pad_note, palette_index) on channel 0 to set a
-- pad's color (Push family convention). Velocity 0 turns the pad off.
-- Move's firmware clears LEDs on its own if not re-asserted, so we keep
-- an in-memory buffer and re-broadcast it on a 1 Hz heartbeat below.
--
-- These color constants are velocity values and correspond to palette
-- indices. They're best guesses based on the Push 2 palette — Move
-- likely differs. If colors look wrong, tweak these numbers and the
-- change takes effect on the next relaunch.

-- Palette values observed from a 1..32 velocity sweep (see docs):
--   vel  2 → red         (good for root)
--   vel 10 → mid-green   (good for in-scale)
--   vel 19 → dark blue   (good for out-of-scale dim)
--   vel 29 → bright yellow  (good for pressed / active)
-- Higher ranges (33..127) not yet mapped.
local COLOR_OFF         = 0
local COLOR_IN_SCALE    = 10    -- mid-green
local COLOR_ROOT        = 2     -- red
local COLOR_OUT_OF_SCALE = 19   -- dark blue (dim; lit only in chromatic mode)
local COLOR_PRESSED     = 29    -- bright yellow

-- Ripple palette (Push-1-style). Ring at Chebyshev-distance N from the
-- pressed pad uses RIPPLE_COLORS[N]. Ripple ends once N > #RIPPLE_COLORS.
-- Visual effect: the pressed pad glows yellow (COLOR_PRESSED) while the
-- wave rolls outward one ring at a time.
local RIPPLE_COLORS = { 15, 17, 6 }   -- cyan → blue → dim brown

-- How many on_tick cycles a single ripple ring lingers before advancing
-- outward. on_tick runs at 30 Hz, so at 5 the wave spends ~166 ms per
-- ring (total ripple duration ≈ 500 ms for a 3-color palette). Bump
-- higher for a slower, more dramatic wave; lower for snappier feedback.
local RIPPLE_TICKS_PER_STEP = 5

-- Desired LED state — pad index (0..31) → velocity (0..127). Composed
-- from scale layout + held-pad set, flushed to the device by flush().
local pad_led_buf = {}
for i = 0, 31 do pad_led_buf[i] = COLOR_OFF end

-- Which pads are currently pressed — brightens them above scale color.
local pad_held = {}

-- Active ripple animations. Each entry: {row, col, tick}. `tick`
-- advances by 1 per on_tick call; while it's within #RIPPLE_COLORS we
-- paint a ring at that Chebyshev distance from (row,col). Entries drop
-- out once their tick exceeds the palette length.
local ripples = {}

local function idx_to_rowcol(idx)
    return math.floor(idx / pads.PAD_COLS), idx % pads.PAD_COLS
end

local function rowcol_to_idx(row, col)
    if row < 0 or row >= pads.PAD_ROWS then return nil end
    if col < 0 or col >= pads.PAD_COLS then return nil end
    return row * pads.PAD_COLS + col
end

-- True iff `note` is a member of `intervals` (offsets from root). Used
-- to color pads differently in chromatic mode — scale-mode layouts only
-- ever assign in-scale notes, so this comes out all-positive there and
-- the dim OUT_OF_SCALE color only appears in chromatic mode.
local function is_note_in_scale(note, root, intervals)
    local offset = (note - root) % 12
    for _, iv in ipairs(intervals) do
        if iv == offset then return true end
    end
    return false
end

local function compose_pad_leds()
    local root      = pads.root_note
    local scale     = scales.catalog[pads.scale_index]
    local intervals = (scale and scale.intervals) or {}

    -- Base: scale visualization + held-pad highlight.
    for idx = 0, (pads.PAD_COLS * pads.PAD_ROWS) - 1 do
        if pad_held[idx] then
            pad_led_buf[idx] = COLOR_PRESSED
        else
            local note = pads.index_to_note(idx)
            if note == nil then
                pad_led_buf[idx] = COLOR_OFF
            elseif (note % 12) == root then
                pad_led_buf[idx] = COLOR_ROOT
            elseif is_note_in_scale(note, root, intervals) then
                pad_led_buf[idx] = COLOR_IN_SCALE
            else
                pad_led_buf[idx] = COLOR_OUT_OF_SCALE
            end
        end
    end

    -- Ripple overlay — for each active ripple, paint the Chebyshev ring
    -- at distance=tick with the palette color for that distance. Held
    -- pads keep their COLOR_PRESSED so the origin pad stays bright
    -- while the wave rolls outward.
    for _, r in ipairs(ripples) do
        local dist = r.tick
        local color = RIPPLE_COLORS[dist]
        if color then
            for dr = -dist, dist do
                for dc = -dist, dist do
                    if math.max(math.abs(dr), math.abs(dc)) == dist then
                        local idx = rowcol_to_idx(r.row + dr, r.col + dc)
                        if idx and not pad_held[idx] then
                            pad_led_buf[idx] = color
                        end
                    end
                end
            end
        end
    end
end

-- Advance every active ripple by one tick, dropping ones that have
-- expanded past the palette. Called from on_tick when ripples exist.
local function advance_ripples()
    local alive = {}
    for _, r in ipairs(ripples) do
        r.tick = r.tick + 1
        if r.tick <= #RIPPLE_COLORS then
            alive[#alive + 1] = r
        end
    end
    ripples = alive
end

local function flush_pad_leds()
    for idx = 0, (pads.PAD_COLS * pads.PAD_ROWS) - 1 do
        yawn.midi_send(0x90, pads.PAD_GRID_START + idx, pad_led_buf[idx])
    end
end

local function refresh_pad_leds()
    compose_pad_leds()
    flush_pad_leds()
end

local function clear_pad_leds()
    for i = 0, 31 do pad_led_buf[i] = COLOR_OFF end
    flush_pad_leds()
end

-- ── Lua callbacks ──────────────────────────────────────────────────────────

function on_connect()
    yawn.log("[Move] Connected; painting pad LEDs for scale visualization")
    pads.recompute(scales)
    refresh_pad_leds()
    yawn.toast("Ableton Move connected", 1.5, 0)
end

function on_disconnect()
    yawn.log("[Move] Disconnected; clearing pad LEDs")
    clear_pad_leds()
    yawn.toast("Ableton Move disconnected", 1.5, 1)
end

-- MIDI dispatch. We see raw bytes; interpret by status nibble.
function on_midi(data)
    if #data < 1 then return end
    local status = data[1]
    local st_hi  = status - (status % 16)  -- high nibble (type)
    -- local channel = status % 16       -- low nibble (channel, unused for now)

    -- Note On. The C++ binding treats velocity=0 as Note Off internally,
    -- so both on-pad-press (vel > 0) and on-pad-release (running-status
    -- vel=0) fall through the same call. LED-wise we track the held
    -- state so the pad lights up bright on press and falls back to its
    -- scale color on release.
    if st_hi == 0x90 and #data >= 3 then
        local note = data[2]
        local vel  = data[3]

        -- Main encoder touch — peek at what the next turn will change
        -- (scale if shift held, track otherwise). Fires on press only.
        if note == NOTE_MAIN_ENCODER_TOUCH then
            if vel > 0 then
                if shift_held then toast_scale()
                else               toast_track() end
            end
            return
        end

        -- Master encoder touch — peek at the live volume / BPM.
        if note == NOTE_MASTER_ENCODER_TOUCH then
            if vel > 0 then
                if shift_held then toast_bpm()
                else               toast_master_volume() end
            end
            return
        end

        local idx = pads.hw_note_to_index(note)
        if idx then
            local mapped = pads.index_to_note(idx)
            if mapped then
                local track = yawn.get_selected_track()
                yawn.send_note_to_track(track, mapped, vel, 0)
            end
            if vel > 0 then
                pad_held[idx] = true
                -- Start a ripple from this pad. compose_pad_leds() will
                -- paint the concentric rings while on_tick expands them.
                local row, col = idx_to_rowcol(idx)
                ripples[#ripples + 1] = {row = row, col = col, tick = 1}
            else
                pad_held[idx] = nil
            end
            refresh_pad_leds()
            return
        end
        -- Numbered buttons (E0..G1 = notes 28..43). First 8 = scene
        -- launch; rest logged for future wiring.
        if note >= NOTE_BUTTON_BASE and note <= NOTE_BUTTON_END and vel > 0 then
            local button_idx = note - NOTE_BUTTON_BASE   -- 0..15
            if button_idx < 8 then
                yawn.launch_scene(button_idx)
                yawn.toast("Scene " .. tostring(button_idx + 1))
            else
                log_once("btn_" .. (button_idx + 1),
                    string.format("[Move] button %d pressed (unmapped)", button_idx + 1))
            end
            return
        end
        -- Not a pad or known button: log for discovery
        log_once("note_" .. note,
            string.format("[Move] unknown Note On: note=%d vel=%d", note, vel))
        return
    end

    -- Note Off (explicit 0x80 form)
    if st_hi == 0x80 and #data >= 3 then
        local note = data[2]
        if note == NOTE_MAIN_ENCODER_TOUCH   then return end   -- touch release, no-op
        if note == NOTE_MASTER_ENCODER_TOUCH then return end   -- touch release, no-op
        local idx = pads.hw_note_to_index(note)
        if idx then
            local mapped = pads.index_to_note(idx)
            if mapped then
                local track = yawn.get_selected_track()
                yawn.send_note_to_track(track, mapped, 0, 0)
            end
            pad_held[idx] = nil
            refresh_pad_leds()
        end
        return
    end

    -- Control Change
    if st_hi == 0xB0 and #data >= 3 then
        local cc    = data[2]
        local value = data[3]

        -- Main (top-left, dented) encoder
        if cc == CC_MAIN_ENCODER then
            apply_main_encoder(value)
            return
        end

        -- Master (smooth) encoder
        if cc == CC_MASTER_ENCODER then
            apply_master_encoder(value)
            return
        end

        -- Eight device-param encoders (always respond, press + release alike)
        for _, e in ipairs(ENCODER_CCS) do
            if cc == e then apply_encoder(cc, value); return end
        end

        -- Shift — held modifier, not a press-only action
        if cc == CC_SHIFT then
            shift_held = (value > 0)
            return
        end

        -- Everything below is press-only (value > 0 == button down).
        if value == 0 then return end

        -- Transport
        if cc == CC_PLAY then
            local now = not yawn.is_playing()
            yawn.set_playing(now)
            yawn.toast(now and "Play" or "Stop")
            return
        end
        if cc == CC_RECORD then
            local now = not yawn.is_recording()
            yawn.set_recording(now)
            yawn.toast(now and "Record armed" or "Record off",
                       1.5, now and 1 or 0)
            return
        end

        -- + / − — plain: octave; with Shift: cycle root note semitone.
        -- Both variants recompute the pad layout and refresh LEDs.
        if cc == CC_OCTAVE_UP or cc == CC_OCTAVE_DOWN then
            local dir = (cc == CC_OCTAVE_UP) and 1 or -1
            if shift_held then
                pads.root_note = (pads.root_note + dir + 12) % 12
                pads.recompute(scales)
                refresh_pad_leds()
                toast_root()
            else
                if dir > 0 then pads.octave_up(scales)
                else            pads.octave_down(scales) end
                refresh_pad_leds()
                toast_octave()
            end
            return
        end

        -- < / > — plain: cycle selected track; with Shift: cycle scale.
        if cc == CC_TRACK_LEFT or cc == CC_TRACK_RIGHT then
            local dir = (cc == CC_TRACK_RIGHT) and 1 or -1
            if shift_held then
                local n = #scales.catalog
                pads.scale_index = ((pads.scale_index - 1 + dir + n) % n) + 1
                pads.recompute(scales)
                refresh_pad_leds()
                toast_scale()
            else
                local n = yawn.get_track_count()
                if n > 0 then
                    local t = yawn.get_selected_track()
                    yawn.set_selected_track((t + dir + n) % n)
                    toast_track()
                end
            end
            return
        end

        -- Direct track select (4 physical buttons)
        if cc == CC_TRACK_1 then yawn.set_selected_track(0); toast_track(); return end
        if cc == CC_TRACK_2 then yawn.set_selected_track(1); toast_track(); return end
        if cc == CC_TRACK_3 then yawn.set_selected_track(2); toast_track(); return end
        if cc == CC_TRACK_4 then yawn.set_selected_track(3); toast_track(); return end

        -- Track/Session → toggle pad submode (scale ↔ chromatic).
        -- Visible effect: chromatic mode shows dim OUT_OF_SCALE pads
        -- interspersed with IN_SCALE / ROOT pads. Scale mode shows only
        -- IN_SCALE / ROOT pads (no dim ones) since every pad's note is
        -- guaranteed to be in-scale.
        if cc == CC_MODE_TOGGLE then
            if shift_held then
                cycle_layout_preset()
            else
                pads.toggle_submode(scales)
                refresh_pad_leds()
                toast_submode()
            end
            return
        end

        -- Known-but-unwired buttons — quiet log + a toast so the user
        -- gets immediate feedback that the press was received, with a
        -- warn-colored accent flagging "pending Lua-API work".
        if KNOWN_UNWIRED_CCS[cc] then
            log_once("unwired_" .. cc,
                string.format("[Move] %s pressed (CC %d) — no Lua API yet",
                              KNOWN_UNWIRED_CCS[cc], cc))
            yawn.toast(KNOWN_UNWIRED_CCS[cc] .. " (not implemented)", 1.0, 1)
            return
        end

        -- Genuinely unknown — help discovery
        log_once("cc_" .. cc,
            string.format("[Move] unknown CC: %d value=%d", cc, value))
        return
    end

    -- Pitch bend, aftertouch, etc — log for discovery
    if st_hi == 0xE0 then
        log_once("pb", "[Move] pitch bend received (not yet handled)")
        return
    end
end

-- 30 Hz tick for periodic work.
--
-- LED heartbeat: Move's firmware appears to clear pad LEDs on its own
-- shortly after a NoteOn-to-pad message, probably because it's waiting
-- for Ableton Live's pairing handshake to confirm a host is driving
-- the surface. Re-sending the full pad state on a 1 Hz heartbeat keeps
-- the pads visibly lit without that handshake. ~32 msg/s is negligible
-- for USB MIDI.
local LED_HEARTBEAT_TICKS = 30   -- at 30 Hz → 1 second
local led_heartbeat_counter = 0

-- Counter that paces the ripple animation. When it hits
-- RIPPLE_TICKS_PER_STEP we advance every active ripple by one ring.
local ripple_step_counter = 0

function on_tick()
    local t = yawn.get_selected_track()
    if t ~= last_track then
        last_track = t
    end

    local need_refresh = false

    -- Advance any active ripple animations at a throttled rate so the
    -- wave is visibly a wave, not a 100 ms blur. The counter only ticks
    -- while ripples exist; it's reset the instant they clear so the next
    -- press starts a fresh cadence.
    if #ripples > 0 then
        ripple_step_counter = ripple_step_counter + 1
        if ripple_step_counter >= RIPPLE_TICKS_PER_STEP then
            ripple_step_counter = 0
            advance_ripples()
            need_refresh = true
        end
    else
        ripple_step_counter = 0
    end

    -- Heartbeat: Move's firmware clears pad LEDs on its own without
    -- Ableton's pairing handshake. Re-send full state once a second.
    led_heartbeat_counter = led_heartbeat_counter + 1
    if led_heartbeat_counter >= LED_HEARTBEAT_TICKS then
        led_heartbeat_counter = 0
        need_refresh = true
    end

    if need_refresh then refresh_pad_leds() end
end
