-- Ableton Push 1 controller script for YAWN
-- Supports: pad modes (note/drum/session), scale selection, encoders, SysEx display

local scales = require("scales")
local pads   = require("pads")

-- ── Push 1 CC constants ─────────────────────────────────────────────────────

-- Encoders (channel 0): CC 71-78
local ENCODER_CCS = {71, 72, 73, 74, 75, 76, 77, 78}

-- Encoder touch notes: notes 0-7 (param encoders) + 10 (master encoder)
local ENCODER_TOUCH_NOTES = {}
for i = 0, 10 do ENCODER_TOUCH_NOTES[i] = true end

-- Encoder sensitivity
local ENCODER_SENSITIVITY_COARSE = 0.05
local ENCODER_SENSITIVITY_FINE   = 0.005

-- Master encoder sensitivity
local MASTER_VOL_COARSE = 0.05
local MASTER_VOL_FINE   = 0.005

-- Navigation
local CC_LEFT       = 44   -- Left arrow
local CC_RIGHT      = 45   -- Right arrow
local CC_UP         = 46   -- Up arrow
local CC_DOWN       = 47   -- Down arrow

-- Transport & utility
local CC_PLAY       = 85
local CC_METRONOME  = 9
local CC_TAP_TEMPO  = 3
local CC_MASTER     = 79   -- Master encoder (relative)
local CC_TEMPO      = 14   -- Tempo encoder (dented, relative)
local CC_SHIFT      = 49   -- Shift (held modifier)

-- Mode & scale buttons
local CC_NOTE_MODE  = 50   -- Note button
local CC_SESSION    = 51   -- Session button
local CC_SCALE      = 58   -- Scale button
local CC_OCTAVE_UP  = 55   -- Octave Up
local CC_OCTAVE_DN  = 54   -- Octave Down
local CC_RECORD     = 86   -- Record button

-- Scene launch buttons (right side column, top to bottom: 43..36)
local CC_SCENE_LAUNCH_BASE = 36  -- CC 36 (bottom) to CC 43 (top)

-- Track select buttons (top row above pads, left to right: 20..27)
local CC_TRACK_SELECT_BASE = 20  -- CC 20 (track 1) to CC 27 (track 8)

-- Track arm buttons (bottom row below pads, left to right: 102..109)
local CC_TRACK_STATE_BASE = 102  -- CC 102 (track 1) to CC 109 (track 8)

-- Pad LED colors for ripple effect
local PAD_COLORS = {127, 125, 123, 121}

-- Push 1 SysEx display
local SYSEX_HEADER = {0xF0, 0x47, 0x7F, 0x15}
local DISPLAY_LINES = {0x18, 0x19, 0x1A, 0x1B}
local CLEAR_LINES = {0x1C, 0x1D, 0x1E, 0x1F}

-- Stepped param debounce
local step_debounce = {}
local STEP_DEBOUNCE_TIME = 0.12

-- ── State ───────────────────────────────────────────────────────────────────

local display_dirty = true
local last_selected_track = -1
local param_page = 0
local tap_flash = 0
local shift_held = false
local scale_edit_active = false

-- Touch strip state
local touch_strip_active = false  -- true while finger is on strip
local touch_strip_ticks = 0       -- ticks since last pitch bend received
local TOUCH_STRIP_TIMEOUT = 3     -- ticks (~100ms) before snap-back
local PB_CENTER = 8192            -- 14-bit pitch bend center

-- Ripple animations: {row, col, tick}
local ripples = {}
-- Currently held pads (hw_note → true)
local held_pads = {}

-- ── Helpers ─────────────────────────────────────────────────────────────────

local function set_button_led(cc, on)
    yawn.midi_send(0xB0, cc, on and 127 or 0)
end

local function set_button_led_color(cc, color)
    yawn.midi_send(0xB0, cc, color)
end

local function set_pad_led(note, color)
    if note and note >= pads.PAD_GRID_START and note <= pads.PAD_GRID_END then
        yawn.midi_send(0x90, note, color)
    end
end

-- ── Display ─────────────────────────────────────────────────────────────────

local function send_display_line(line_id, text)
    if #text < 68 then
        text = text .. string.rep(" ", 68 - #text)
    else
        text = text:sub(1, 68)
    end
    local sysex = {}
    for _, b in ipairs(SYSEX_HEADER) do sysex[#sysex + 1] = b end
    sysex[#sysex + 1] = line_id
    sysex[#sysex + 1] = 0x00
    sysex[#sysex + 1] = 68 + 1
    sysex[#sysex + 1] = 0x00
    for i = 1, 68 do sysex[#sysex + 1] = string.byte(text, i) end
    sysex[#sysex + 1] = 0xF7
    yawn.midi_send_sysex(sysex)
end

local function clear_display()
    for _, clear_id in ipairs(CLEAR_LINES) do
        local sysex = {}
        for _, b in ipairs(SYSEX_HEADER) do sysex[#sysex + 1] = b end
        sysex[#sysex + 1] = clear_id
        sysex[#sysex + 1] = 0x00
        sysex[#sysex + 1] = 0x00
        sysex[#sysex + 1] = 0xF7
        yawn.midi_send_sysex(sysex)
    end
end

local function update_display()
    -- Scale edit mode: show scale selection UI
    if scale_edit_active then
        local l1, l2, l3, l4 = pads.get_scale_edit_display(scales)
        send_display_line(DISPLAY_LINES[1], l1)
        send_display_line(DISPLAY_LINES[2], l2)
        send_display_line(DISPLAY_LINES[3], l3)
        send_display_line(DISPLAY_LINES[4], l4)
        return
    end

    -- Session mode: show track/clip grid info
    if pads.mode == pads.MODE_SESSION then
        local l1, l2, l3, l4 = pads.get_session_display()
        send_display_line(DISPLAY_LINES[1], l1)
        send_display_line(DISPLAY_LINES[2], l2)
        send_display_line(DISPLAY_LINES[3], l3)
        send_display_line(DISPLAY_LINES[4], l4)
        return
    end

    local track = yawn.get_selected_track()
    local param_count = yawn.get_device_param_count("instrument", 0)

    -- Clamp page
    local max_page = math.max(0, math.ceil(param_count / 8) - 1)
    if param_page > max_page then param_page = max_page end
    local page_offset = param_page * 8

    -- Lines 1-2: parameter names and values
    local names_line = ""
    local values_line = ""
    for i = 0, 7 do
        local col_width = 8
        local name = ""
        local val_str = ""
        local pi = page_offset + i
        if pi < param_count then
            name = yawn.get_device_param_name("instrument", 0, pi) or ""
            val_str = yawn.get_device_param_display("instrument", 0, pi) or ""
        end
        if #name > col_width then name = name:sub(1, col_width) end
        if #val_str > col_width then val_str = val_str:sub(1, col_width) end
        name = name .. string.rep(" ", col_width - #name)
        val_str = val_str .. string.rep(" ", col_width - #val_str)
        if i < 7 then
            names_line = names_line .. name .. " "
            values_line = values_line .. val_str .. " "
        else
            names_line = names_line .. name
            values_line = values_line .. val_str
        end
    end

    -- Line 3: track name + page indicator + pad mode
    local track_name = yawn.get_track_name(track) or ("Track " .. (track + 1))
    local total_pages = max_page + 1
    if total_pages > 1 then
        local page_str = string.format(" [%d/%d]", param_page + 1, total_pages)
        local arrows = ""
        if param_page > 0 then arrows = arrows .. "<" end
        if param_page < max_page then arrows = arrows .. ">" end
        page_str = page_str .. " " .. arrows
        track_name = track_name .. page_str
    end

    -- Line 4: tap tempo flash, or instrument + pad mode info
    local line4
    if tap_flash > 0 then
        line4 = string.format("TAP TEMPO: %.1f BPM", yawn.get_bpm())
    else
        local inst_name = yawn.get_instrument_name(track) or ""
        local mode_name = pads.get_mode_name()
        if mode_name ~= "" then
            line4 = inst_name .. "  |  " .. mode_name
        else
            line4 = inst_name
        end
    end

    send_display_line(DISPLAY_LINES[1], names_line)
    send_display_line(DISPLAY_LINES[2], values_line)
    send_display_line(DISPLAY_LINES[3], track_name)
    send_display_line(DISPLAY_LINES[4], line4)
end

-- ── Ripple Effect ───────────────────────────────────────────────────────────

local function pad_to_grid(note)
    if note < pads.PAD_GRID_START or note > pads.PAD_GRID_END then return nil, nil end
    local idx = note - pads.PAD_GRID_START
    return math.floor(idx / pads.PAD_COLS), idx % pads.PAD_COLS
end

local function grid_to_pad(row, col)
    if row < 0 or row > 7 or col < 0 or col > 7 then return nil end
    return pads.PAD_GRID_START + row * pads.PAD_COLS + col
end

local function draw_ripple(row, col, radius, color)
    for dr = -radius, radius do
        for dc = -radius, radius do
            if math.max(math.abs(dr), math.abs(dc)) == radius then
                local pad = grid_to_pad(row + dr, col + dc)
                if pad and not (color == 0 and held_pads[pad]) then
                    if color == 0 then
                        -- Restore mode-appropriate LED color instead of off
                        pads.restore_pad_led(pad)
                    else
                        set_pad_led(pad, color)
                    end
                end
            end
        end
    end
end

local function update_ripples()
    local still_active = {}
    for _, r in ipairs(ripples) do
        r.tick = r.tick + 1
        local row, col = r.row, r.col
        if r.tick > 1 then
            draw_ripple(row, col, r.tick - 1, 0)
        end
        if r.tick <= #PAD_COLORS then
            draw_ripple(row, col, r.tick, PAD_COLORS[r.tick])
            still_active[#still_active + 1] = r
        end
    end
    ripples = still_active
end

-- ── Encoders ────────────────────────────────────────────────────────────────

local function decode_relative(raw_value)
    if raw_value >= 1 and raw_value <= 63 then
        return raw_value
    elseif raw_value >= 65 and raw_value <= 127 then
        return raw_value - 128
    end
    return 0
end

local function handle_encoder_scale_edit(encoder_index, raw_value)
    local delta = decode_relative(raw_value)
    if delta == 0 then return end
    local dir = (delta > 0) and 1 or -1

    if encoder_index == 1 then
        -- Root note (0-11, wrapping)
        pads.root_note = (pads.root_note + dir) % 12
    elseif encoder_index == 2 then
        -- Scale type (cycle through catalog)
        local count = #scales.catalog
        pads.scale_index = ((pads.scale_index - 1 + dir) % count) + 1
    elseif encoder_index == 3 then
        -- Row interval (1-7 semitones)
        pads.row_interval = math.max(1, math.min(7, pads.row_interval + dir))
    elseif encoder_index == 4 then
        -- Octave (0-8)
        pads.octave = math.max(0, math.min(8, pads.octave + dir))
    end

    pads.cleanup_active_notes()
    pads.compute_note_grid(scales)
    pads.update_leds()
    display_dirty = true
end

local function handle_encoder(encoder_index, raw_value)
    -- In scale edit mode, encoders control scale parameters
    if scale_edit_active then
        handle_encoder_scale_edit(encoder_index, raw_value)
        return
    end

    local delta = decode_relative(raw_value)
    if delta == 0 then return end

    local param_count = yawn.get_device_param_count("instrument", 0)
    local param_idx = param_page * 8 + (encoder_index - 1)
    if param_idx >= param_count then return end

    local cur = yawn.get_device_param_value("instrument", 0, param_idx)
    local lo  = yawn.get_device_param_min("instrument", 0, param_idx)
    local hi  = yawn.get_device_param_max("instrument", 0, param_idx)
    local range = hi - lo
    if range <= 0 then return end

    local label_count = yawn.get_device_param_label_count("instrument", 0, param_idx)
    local new_val
    if label_count > 0 then
        local now = os.clock()
        local last = step_debounce[param_idx] or 0
        if now - last < STEP_DEBOUNCE_TIME then return end
        step_debounce[param_idx] = now
        local step = (delta > 0) and 1 or -1
        new_val = math.floor(cur + 0.5) + step
    else
        local sensitivity = shift_held and ENCODER_SENSITIVITY_FINE or ENCODER_SENSITIVITY_COARSE
        new_val = cur + delta * sensitivity * range
    end

    if new_val < lo then new_val = lo end
    if new_val > hi then new_val = hi end
    yawn.set_device_param("instrument", 0, param_idx, new_val)
    display_dirty = true
end

local function handle_tempo_encoder(raw_value)
    local delta = decode_relative(raw_value)
    if delta == 0 then return end
    local bpm = yawn.get_bpm()
    local bpm_step = shift_held and 0.1 or 1.0
    bpm = bpm + delta * bpm_step
    if bpm < 20 then bpm = 20 end
    if bpm > 999 then bpm = 999 end
    yawn.set_bpm(bpm)
    display_dirty = true
end

local function handle_master_encoder(raw_value)
    local delta = decode_relative(raw_value)
    if delta == 0 then return end
    local cur = yawn.get_master_volume()
    local sens = shift_held and MASTER_VOL_FINE or MASTER_VOL_COARSE
    local new_val = cur + delta * sens
    if new_val < 0.0 then new_val = 0.0 end
    if new_val > 2.0 then new_val = 2.0 end
    yawn.set_master_volume(new_val)
    display_dirty = true
end

local function handle_tap_tempo()
    yawn.tap_tempo()
    tap_flash = 30
    display_dirty = true
    set_button_led(CC_TAP_TEMPO, true)
end

-- ── Mode switching helpers ──────────────────────────────────────────────────

local function recompute_pads()
    pads.compute_note_grid(scales)
    pads.update_leds()
    display_dirty = true
end

local function switch_to_note_mode()
    if pads.mode ~= pads.MODE_NOTE then
        pads.cleanup_active_notes()
        pads.mode = pads.MODE_NOTE
        scale_edit_active = false
        pads.sync_session_focus()
        recompute_pads()
    end
end

local function switch_to_session_mode()
    if pads.mode ~= pads.MODE_SESSION then
        pads.cleanup_active_notes()
        pads.mode = pads.MODE_SESSION
        scale_edit_active = false
        pads.sync_session_focus()
        recompute_pads()
    end
end

-- ── MIDI Callbacks ──────────────────────────────────────────────────────────

function on_midi(data)
    if #data < 1 then return end

    local status = data[1]
    local d1 = data[2] or 0
    local d2 = data[3] or 0
    local msg_type = status & 0xF0
    local channel = status & 0x0F

    -- CC on channel 0
    if msg_type == 0xB0 and channel == 0 then
        -- Shift
        if d1 == CC_SHIFT then
            shift_held = (d2 > 0)
            return
        end

        -- Encoders 1-8
        for i, enc_cc in ipairs(ENCODER_CCS) do
            if d1 == enc_cc then
                handle_encoder(i, d2)
                return
            end
        end

        -- Master encoder
        if d1 == CC_MASTER then
            handle_master_encoder(d2)
            return
        end

        -- Tempo encoder
        if d1 == CC_TEMPO then
            handle_tempo_encoder(d2)
            return
        end

        -- Left/Right: session navigate or param page
        if d1 == CC_LEFT and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                pads.session_navigate(-1, 0)
                pads.update_leds()
                display_dirty = true
            elseif param_page > 0 then
                param_page = param_page - 1
                display_dirty = true
            end
            return
        end
        if d1 == CC_RIGHT and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                pads.session_navigate(1, 0)
                pads.update_leds()
                display_dirty = true
            else
                local param_count = yawn.get_device_param_count("instrument", 0)
                local max_page = math.max(0, math.ceil(param_count / 8) - 1)
                if param_page < max_page then
                    param_page = param_page + 1
                    display_dirty = true
                end
            end
            return
        end

        -- Up/Down: session navigate or drum bank
        if d1 == CC_UP and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                pads.session_navigate(0, -1)  -- scroll scenes up (lower scene numbers)
                pads.update_leds()
                display_dirty = true
            elseif pads.mode == pads.MODE_DRUM then
                pads.cleanup_active_notes()
                pads.drum_bank = math.min(pads.drum_bank + 1, 5)
                recompute_pads()
            end
            return
        end
        if d1 == CC_DOWN and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                pads.session_navigate(0, 1)  -- scroll scenes down (higher scene numbers)
                pads.update_leds()
                display_dirty = true
            elseif pads.mode == pads.MODE_DRUM then
                pads.cleanup_active_notes()
                pads.drum_bank = math.max(pads.drum_bank - 1, 0)
                recompute_pads()
            end
            return
        end

        -- Play
        if d1 == CC_PLAY and d2 > 0 then
            local playing = yawn.is_playing()
            yawn.set_playing(not playing)
            set_button_led(CC_PLAY, not playing)
            return
        end

        -- Metronome
        if d1 == CC_METRONOME and d2 > 0 then
            local enabled = yawn.get_metronome_enabled()
            yawn.set_metronome_enabled(not enabled)
            set_button_led(CC_METRONOME, not enabled)
            return
        end

        -- Tap tempo
        if d1 == CC_TAP_TEMPO and d2 > 0 then
            handle_tap_tempo()
            return
        end

        -- Record
        if d1 == CC_RECORD and d2 > 0 then
            if shift_held then
                -- Shift+Record: toggle arm on selected track
                local track = yawn.get_selected_track()
                local armed = yawn.is_track_armed(track)
                yawn.set_track_armed(track, not armed)
            else
                -- Record: toggle transport recording
                local recording = yawn.is_recording()
                yawn.set_recording(not recording, 0)
            end
            display_dirty = true
            return
        end

        -- Scene launch buttons (CC 36-43, right side column)
        if d1 >= CC_SCENE_LAUNCH_BASE and d1 <= CC_SCENE_LAUNCH_BASE + 7 and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                -- Top button (CC 43) = first visible scene, bottom (CC 36) = last
                local btn_row = d1 - CC_SCENE_LAUNCH_BASE  -- 0 (bottom) .. 7 (top)
                local scene = pads.session_scene_offset + (7 - btn_row)
                yawn.launch_scene(scene)
            end
            return
        end

        -- Track select buttons (CC 20-27, top row above pads)
        if d1 >= CC_TRACK_SELECT_BASE and d1 <= CC_TRACK_SELECT_BASE + 7 and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                local col = d1 - CC_TRACK_SELECT_BASE
                local track = pads.session_track_offset + col
                local num_tracks = yawn.get_track_count()
                if track < num_tracks then
                    yawn.set_selected_track(track)
                    display_dirty = true
                end
            end
            return
        end

        -- Track arm buttons (CC 102-109, bottom row below pads)
        if d1 >= CC_TRACK_STATE_BASE and d1 <= CC_TRACK_STATE_BASE + 7 and d2 > 0 then
            if pads.mode == pads.MODE_SESSION then
                local col = d1 - CC_TRACK_STATE_BASE
                local track = pads.session_track_offset + col
                local num_tracks = yawn.get_track_count()
                if track < num_tracks then
                    local armed = yawn.is_track_armed(track)
                    yawn.set_track_armed(track, not armed)
                    display_dirty = true
                end
            end
            return
        end

        -- ── Mode buttons ────────────────────────────────────────────────

        -- Note mode
        if d1 == CC_NOTE_MODE and d2 > 0 then
            switch_to_note_mode()
            return
        end

        -- Session mode
        if d1 == CC_SESSION and d2 > 0 then
            switch_to_session_mode()
            return
        end

        -- Scale button: toggle scale mode / enter/exit scale edit
        -- Shift+Scale: toggle back to chromatic
        if d1 == CC_SCALE and d2 > 0 then
            if pads.mode == pads.MODE_NOTE then
                if shift_held then
                    -- Shift+Scale: toggle between chromatic and scale
                    pads.cleanup_active_notes()
                    if pads.note_submode == pads.SUBMODE_SCALE then
                        pads.note_submode = pads.SUBMODE_CHROMATIC
                    else
                        pads.note_submode = pads.SUBMODE_SCALE
                    end
                    scale_edit_active = false
                    recompute_pads()
                elseif scale_edit_active then
                    -- Exit scale edit
                    scale_edit_active = false
                    display_dirty = true
                else
                    -- Enter scale edit (switch to scale mode if chromatic)
                    if pads.note_submode == pads.SUBMODE_CHROMATIC then
                        pads.note_submode = pads.SUBMODE_SCALE
                    end
                    scale_edit_active = true
                    pads.cleanup_active_notes()
                    recompute_pads()
                end
            end
            return
        end

        -- Octave Up
        if d1 == CC_OCTAVE_UP and d2 > 0 then
            if pads.mode == pads.MODE_NOTE then
                pads.cleanup_active_notes()
                pads.octave = math.min(pads.octave + 1, 8)
                recompute_pads()
            end
            return
        end

        -- Octave Down
        if d1 == CC_OCTAVE_DN and d2 > 0 then
            if pads.mode == pads.MODE_NOTE then
                pads.cleanup_active_notes()
                pads.octave = math.max(pads.octave - 1, 0)
                recompute_pads()
            end
            return
        end

    end

    -- Note On / Note Off
    if msg_type == 0x90 or msg_type == 0x80 then
        -- Filter encoder touch notes
        if channel == 0 and ENCODER_TOUCH_NOTES[d1] then
            return
        end

        -- Pad range: handle via pad mode
        if d1 >= pads.PAD_GRID_START and d1 <= pads.PAD_GRID_END then
            local velocity = (msg_type == 0x90) and d2 or 0
            pads.handle_pad_note(d1, velocity, channel)

            -- Visual feedback: ripple on press, restore mode color on release
            if velocity > 0 then
                held_pads[d1] = true
                set_pad_led(d1, PAD_COLORS[1])
                local row, col = pad_to_grid(d1)
                if row then
                    ripples[#ripples + 1] = {row = row, col = col, tick = 0}
                end
            else
                held_pads[d1] = nil
                pads.restore_pad_led(d1)
            end
            return
        end

        -- Non-pad notes: forward directly
        local track = yawn.get_selected_track()
        local velocity = (msg_type == 0x90) and d2 or 0
        yawn.send_note_to_track(track, d1, velocity, channel)
        return
    end

    -- Polyphonic aftertouch: remap through pad mode
    if msg_type == 0xA0 then
        if d1 >= pads.PAD_GRID_START and d1 <= pads.PAD_GRID_END then
            pads.handle_aftertouch(d1, d2)
        end
        return
    end

    -- Pitch bend (0xE0): touch strip
    if msg_type == 0xE0 then
        local val14 = d1 + (d2 * 128)  -- 14-bit pitch bend value
        local track = yawn.get_selected_track()

        if shift_held then
            -- Shift + touch strip = mod wheel (CC 1), stays where you leave it
            local cc_val = d2  -- use MSB (7-bit) as mod wheel value
            yawn.send_cc_to_track(track, 1, cc_val, channel)
        else
            -- Default = pitch bend, snap back on release
            touch_strip_active = true
            touch_strip_ticks = 0
            yawn.send_pitchbend_to_track(track, val14, channel)
        end
        return
    end
end

function on_tick()
    -- Track selection changed → auto-switch pad mode
    local track = yawn.get_selected_track()
    if track ~= last_selected_track then
        last_selected_track = track
        param_page = 0
        local changed = pads.auto_switch_mode(track)
        if changed then
            scale_edit_active = false
        end
        pads.compute_note_grid(scales)
        pads.update_leds()
        display_dirty = true
    end

    -- Touch strip pitch bend snap-back
    if touch_strip_active then
        touch_strip_ticks = touch_strip_ticks + 1
        if touch_strip_ticks >= TOUCH_STRIP_TIMEOUT then
            -- No pitch bend received recently: finger lifted, snap to center
            local track = yawn.get_selected_track()
            yawn.send_pitchbend_to_track(track, PB_CENTER, 0)
            touch_strip_active = false
        end
    end

    -- Tap tempo flash countdown
    if tap_flash > 0 then
        tap_flash = tap_flash - 1
        display_dirty = true
        if tap_flash == 0 then
            set_button_led(CC_TAP_TEMPO, false)
        end
    end

    -- Sync button LEDs with engine/mode state
    set_button_led(CC_PLAY, yawn.is_playing())
    set_button_led(CC_METRONOME, yawn.get_metronome_enabled())
    set_button_led(CC_NOTE_MODE, pads.mode == pads.MODE_NOTE)
    set_button_led(CC_SESSION, pads.mode == pads.MODE_SESSION)
    set_button_led(CC_SCALE, pads.mode == pads.MODE_NOTE and pads.note_submode == pads.SUBMODE_SCALE)
    set_button_led(CC_RECORD, yawn.is_recording())

    -- Session mode: refresh pad LEDs and scene launch buttons every tick
    if pads.mode == pads.MODE_SESSION then
        pads.update_leds()

        -- Scene launch button LEDs (CC 36-43)
        local num_scenes = yawn.get_num_scenes()
        local num_tracks = yawn.get_track_count()
        for btn = 0, 7 do
            local scene = pads.session_scene_offset + (7 - btn)
            local color = 0  -- off
            if scene < num_scenes then
                -- Check if any clip in this scene is playing or recording
                local has_clip = false
                local scene_playing = false
                local scene_recording = false
                for t = 0, num_tracks - 1 do
                    local st = yawn.get_clip_slot_state(t, scene)
                    if st then
                        if st.type ~= "empty" then has_clip = true end
                        if st.playing then scene_playing = true end
                        if st.recording then scene_recording = true end
                    end
                end
                if scene_recording then
                    color = 4    -- red (recording)
                elseif scene_playing then
                    color = 127  -- green (playing)
                elseif has_clip then
                    color = 122  -- amber (has clips)
                else
                    color = 3    -- dim (empty but valid scene)
                end
            end
            set_button_led_color(CC_SCENE_LAUNCH_BASE + btn, color)
        end

        -- Track select button LEDs (CC 20-27): on for selected track
        local selected = yawn.get_selected_track()
        for col = 0, 7 do
            local track = pads.session_track_offset + col
            if track < num_tracks then
                set_button_led(CC_TRACK_SELECT_BASE + col, track == selected)
            else
                set_button_led(CC_TRACK_SELECT_BASE + col, false)
            end
        end

        -- Track arm button LEDs (CC 102-109): on when armed
        for col = 0, 7 do
            local track = pads.session_track_offset + col
            if track < num_tracks then
                local armed = yawn.is_track_armed(track)
                set_button_led(CC_TRACK_STATE_BASE + col, armed)
            else
                set_button_led(CC_TRACK_STATE_BASE + col, false)
            end
        end

        display_dirty = true  -- update transport status on display
    else
        -- Turn off scene launch, track select, and arm LEDs when not in session mode
        for btn = 0, 7 do
            set_button_led_color(CC_SCENE_LAUNCH_BASE + btn, 0)
            set_button_led(CC_TRACK_SELECT_BASE + btn, false)
            set_button_led(CC_TRACK_STATE_BASE + btn, false)
        end
    end

    if display_dirty then
        update_display()
        display_dirty = false
    end

    -- Animate pad ripples
    if #ripples > 0 then
        update_ripples()
    end
end

function on_connect()
    yawn.log("Ableton Push 1 connected")

    clear_display()
    send_display_line(DISPLAY_LINES[1], "                Y . A . W . N                   ")
    send_display_line(DISPLAY_LINES[2], "      Yet Another Audio Workstation New          ")
    send_display_line(DISPLAY_LINES[3], "")
    send_display_line(DISPLAY_LINES[4], "            Fasten your seatbelts!               ")

    -- Initialize pad mode
    last_selected_track = yawn.get_selected_track()
    pads.auto_switch_mode(last_selected_track)
    pads.compute_note_grid(scales)
    pads.update_leds()
    display_dirty = true
end

function on_disconnect()
    yawn.log("Ableton Push 1 disconnected")
    pads.cleanup_active_notes()
    clear_display()
end
