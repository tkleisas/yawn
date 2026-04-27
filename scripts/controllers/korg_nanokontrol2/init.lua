-- Korg nanoKONTROL2 controller script for YAWN
-- Maps faders → track volume, knobs → pan, buttons → mute/solo/arm/transport
--
-- The nanoKONTROL2 should be in CC mode (power-on default).
-- If buttons behave unexpectedly, use the Korg Kontrol Editor to
-- reset to factory defaults or set all controls to CC Momentary mode.
--
-- CC map (default Sonar mode):
--   Faders 1-8: CC 0-7     (0–127 absolute)
--   Knobs 1-8:  CC 16-23   (0–127 absolute)
--   Solo 1-8:   CC 8-15    (127 = pressed, 0 = released)
--   Mute 1-8:   CC 48-55   (127 = pressed, 0 = released)
--   Rec 1-8:    CC 56-63   (127 = pressed, 0 = released)
--   Transport:  Play=41, Stop=42, Rew=43, Fwd=44, Rec=45, Cycle=46
--   Track nav:  Left=58, Right=59
--   Marker:     Set=60, L=61, R=62

-- ── CC constants ────────────────────────────────────────────────────────────

local CC_FADER_BASE   = 0    -- CC 0–7: faders
local CC_KNOB_BASE    = 16   -- CC 16–23: knobs
local CC_SOLO_BASE    = 8    -- CC 8–15: solo buttons
local CC_MUTE_BASE    = 48   -- CC 48–55: mute buttons
local CC_REC_BASE     = 56   -- CC 56–63: rec buttons

local CC_PLAY         = 41
local CC_STOP         = 42
local CC_REW          = 43
local CC_FWD          = 44
local CC_REC          = 45
local CC_CYCLE        = 46
local CC_TRACK_LEFT   = 58
local CC_TRACK_RIGHT  = 59
local CC_MARKER_SET   = 60
local CC_MARKER_LEFT  = 61
local CC_MARKER_RIGHT = 62

-- ── State ───────────────────────────────────────────────────────────────────

local track_offset = 0     -- which 8-channel bank is visible
local loop_active = false
local selected_track = 0
local mute_states   = {}   -- track muted?
local solo_states   = {}   -- track soloed?
local rec_states    = {}   -- track armed?
local fader_values  = {}   -- last fader values for LED persistence
local last_fader_cc = {}   -- debounce: don't send LED feedback for fader moves

-- ── LED feedback helpers ────────────────────────────────────────────────────

-- nanoKONTROL2 LEDs respond to MIDI output on the same CC as the button:
--   127 = LED on, 0 = LED off
-- Fader/knob movement LEDs are handled per-controller, we leave them alone.

local function set_led(cc, on)
    yawn.midi_send(0xB0, cc, on and 127 or 0)
end

local function refresh_mute_leds()
    for i = 0, 7 do
        local t = track_offset + i
        set_led(CC_MUTE_BASE + i, mute_states[t] == true)
    end
end

local function refresh_solo_leds()
    for i = 0, 7 do
        local t = track_offset + i
        set_led(CC_SOLO_BASE + i, solo_states[t] == true)
    end
end

local function refresh_rec_leds()
    for i = 0, 7 do
        local t = track_offset + i
        set_led(CC_REC_BASE + i, rec_states[t] == true)
    end
end

-- ── Track helpers ───────────────────────────────────────────────────────────

local function track_count()
    return yawn.get_track_count() or 0
end

local function max_offset()
    local tc = track_count()
    return math.max(0, tc - 8)
end

local function select_track(t)
    t = math.max(0, math.min(track_count() - 1, t))
    yawn.set_selected_track(t)
    selected_track = t
    yawn.toast("T" .. (t + 1), 0.8)
end

-- ── Volume / pan mapping helpers ────────────────────────────────────────────

local function cc_to_vol(cc_val)
    -- CC 0-127 → volume 0.0-2.0 (matching YAWN's internal 0-2 fader range)
    return cc_val / 127.0 * 2.0
end

local function cc_to_pan(cc_val)
    -- CC 0-127 → pan -1.0 to +1.0
    return (cc_val / 127.0) * 2.0 - 1.0
end

-- ── Callbacks ───────────────────────────────────────────────────────────────

function on_connect()
    yawn.log("nanoKONTROL2 connected")
    track_offset = 0
    selected_track = yawn.get_selected_track() or 0
    loop_active = yawn.get_loop()

    -- sync state from engine
    local tc = track_count()
    for t = 0, tc - 1 do
        mute_states[t] = yawn.get_track_mute(t)
        solo_states[t] = yawn.get_track_solo(t)
        rec_states[t]  = yawn.is_track_armed(t)
    end

    refresh_mute_leds()
    refresh_solo_leds()
    refresh_rec_leds()
    set_led(CC_CYCLE, loop_active)
    yawn.toast("nanoKONTROL2 ready", 1.0)
end

function on_disconnect()
    yawn.log("nanoKONTROL2 disconnected")
end

function on_midi(msg)
    if #msg < 3 then return end

    local status = msg[1]
    local cc     = msg[2]
    local val    = msg[3]

    -- only handle CC messages (0xB0 = CC on channel 1)
    if status ~= 0xB0 then return end

    -- ── Faders (CC 0-7) → track volume ──
    if cc >= CC_FADER_BASE and cc <= CC_FADER_BASE + 7 then
        local i = cc - CC_FADER_BASE
        local t = track_offset + i
        if t >= 0 and t < track_count() then
            local vol = cc_to_vol(val)
            yawn.set_track_volume(t, vol)
            fader_values[t] = vol
        end
        return
    end

    -- ── Knobs (CC 16-23) → track pan ──
    if cc >= CC_KNOB_BASE and cc <= CC_KNOB_BASE + 7 then
        local i = cc - CC_KNOB_BASE
        local t = track_offset + i
        if t >= 0 and t < track_count() then
            yawn.set_track_pan(t, cc_to_pan(val))
        end
        return
    end

    -- ── Solo buttons (CC 8-15) → toggle track solo ──
    if cc >= CC_SOLO_BASE and cc <= CC_SOLO_BASE + 7 then
        if val == 127 then
            local i = cc - CC_SOLO_BASE
            local t = track_offset + i
            if t >= 0 and t < track_count() then
                local new_state = not (solo_states[t] or false)
                yawn.set_track_solo(t, new_state)
                solo_states[t] = new_state
                set_led(cc, new_state)
            end
        end
        return
    end

    -- ── Mute buttons (CC 48-55) → toggle track mute ──
    if cc >= CC_MUTE_BASE and cc <= CC_MUTE_BASE + 7 then
        if val == 127 then
            local i = cc - CC_MUTE_BASE
            local t = track_offset + i
            if t >= 0 and t < track_count() then
                local new_state = not (mute_states[t] or false)
                yawn.set_track_mute(t, new_state)
                mute_states[t] = new_state
                set_led(cc, new_state)
            end
        end
        return
    end

    -- ── Rec buttons (CC 56-63) → arm track ──
    if cc >= CC_REC_BASE and cc <= CC_REC_BASE + 7 then
        if val == 127 then
            local i = cc - CC_REC_BASE
            local t = track_offset + i
            if t >= 0 and t < track_count() then
                local new_state = not (rec_states[t] or false)
                yawn.set_track_armed(t, new_state)
                rec_states[t] = new_state
                set_led(cc, new_state)
            end
        end
        return
    end

    -- ── Transport ──
    if cc == CC_PLAY then
        if val == 127 then
            if yawn.is_playing() then
                yawn.set_playing(false)
            else
                yawn.set_playing(true)
            end
        end
        return
    end

    if cc == CC_STOP then
        if val == 127 then
            yawn.set_playing(false)
        end
        return
    end

    if cc == CC_REC then
        if val == 127 then
            local rec = yawn.is_recording()
            yawn.set_recording(not rec, 0)
        end
        return
    end

    if cc == CC_CYCLE then
        if val == 127 then
            loop_active = not loop_active
            yawn.set_loop(loop_active)
            set_led(CC_CYCLE, loop_active)
        end
        return
    end

    -- ── Track navigation (Rew/Fwd as track prev/next) ──
    if cc == CC_TRACK_LEFT or cc == CC_REW then
        if val == 127 then
            local t = yawn.get_selected_track() or 0
            select_track(t - 1)
        end
        return
    end

    if cc == CC_TRACK_RIGHT or cc == CC_FWD then
        if val == 127 then
            local t = yawn.get_selected_track() or 0
            select_track(t + 1)
        end
        return
    end

    -- ── Marker Set → re-sync LED state from engine ──
    if cc == CC_MARKER_SET then
        if val == 127 then
            local tc = track_count()
            for t = 0, tc - 1 do
                mute_states[t] = yawn.get_track_mute(t)
                solo_states[t] = yawn.get_track_solo(t)
                rec_states[t]  = yawn.is_track_armed(t)
            end
            refresh_mute_leds()
            refresh_solo_leds()
            refresh_rec_leds()
            set_led(CC_CYCLE, yawn.get_loop())
            yawn.toast("LEDs synced", 0.8)
        end
        return
    end

    -- ── Marker Left → bank left (shift track_offset by -8) ──
    if cc == CC_MARKER_LEFT then
        if val == 127 then
            track_offset = math.max(0, track_offset - 8)
            select_track(track_offset)
            refresh_mute_leds()
            refresh_solo_leds()
            refresh_rec_leds()
            yawn.toast("Bank: T" .. (track_offset + 1) .. "-" .. math.min(track_count(), track_offset + 8), 0.8)
        end
        return
    end

    -- ── Marker Right → bank right (shift track_offset by +8) ──
    if cc == CC_MARKER_RIGHT then
        if val == 127 then
            track_offset = math.min(max_offset(), track_offset + 8)
            select_track(track_offset)
            refresh_mute_leds()
            refresh_solo_leds()
            refresh_rec_leds()
            yawn.toast("Bank: T" .. (track_offset + 1) .. "-" .. math.min(track_count(), track_offset + 8), 0.8)
        end
        return
    end
end

function on_tick()
    -- No per-frame animation needed. LED state is updated on button press
    -- and on_connect. Transport LEDs could be refreshed here if needed.
end
