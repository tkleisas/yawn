-- Ableton Push 1 controller script for YAWN
-- Supports: 8 encoders mapped to device parameters, SysEx text display

-- Push 1 encoder CCs (channel 0): CC 71-78
local ENCODER_CCS = {71, 72, 73, 74, 75, 76, 77, 78}

-- Encoder touch notes: notes 0-7 (filter these, don't forward to instrument)
local ENCODER_TOUCH_NOTES = {}
for i = 0, 7 do ENCODER_TOUCH_NOTES[i] = true end

-- Encoder sensitivity: fraction of parameter range per tick
local ENCODER_SENSITIVITY = 0.01

-- Pad LED colors (Push 1 velocity-to-color palette)
-- Ripple effect: bright center, fading rings
local PAD_COLORS = {127, 125, 123, 121}  -- center → outer rings (bright → dim)
local PAD_GRID_START = 36
local PAD_GRID_END = 99
local PAD_COLS = 8

-- Active ripple animations: {note, tick_count}
local ripples = {}

-- Push 1 SysEx display header
-- Format: F0 47 7F 15 <line> 00 <len+1> <col_offset> <text> F7
-- Lines: 0x18-0x1B (write), 0x1C-0x1F (clear)
local SYSEX_HEADER = {0xF0, 0x47, 0x7F, 0x15}
local DISPLAY_LINES = {0x18, 0x19, 0x1A, 0x1B}
local CLEAR_LINES = {0x1C, 0x1D, 0x1E, 0x1F}

-- State
local display_dirty = true
local last_selected_track = -1

-- ── Display ─────────────────────────────────────────────────────────────────

local function send_display_line(line_id, text)
    -- Pad or trim to exactly 68 characters
    if #text < 68 then
        text = text .. string.rep(" ", 68 - #text)
    else
        text = text:sub(1, 68)
    end

    local sysex = {}
    for _, b in ipairs(SYSEX_HEADER) do
        sysex[#sysex + 1] = b
    end
    sysex[#sysex + 1] = line_id
    sysex[#sysex + 1] = 0x00        -- padding
    sysex[#sysex + 1] = 68 + 1      -- payload length (offset byte + 68 chars)
    sysex[#sysex + 1] = 0x00        -- column offset (start at 0)
    for i = 1, 68 do
        sysex[#sysex + 1] = string.byte(text, i)
    end
    sysex[#sysex + 1] = 0xF7

    yawn.midi_send_sysex(sysex)
end

local function clear_display()
    for _, clear_id in ipairs(CLEAR_LINES) do
        local sysex = {}
        for _, b in ipairs(SYSEX_HEADER) do
            sysex[#sysex + 1] = b
        end
        sysex[#sysex + 1] = clear_id
        sysex[#sysex + 1] = 0x00
        sysex[#sysex + 1] = 0x00
        sysex[#sysex + 1] = 0xF7
        yawn.midi_send_sysex(sysex)
    end
end

local function update_display()
    local track = yawn.get_selected_track()
    local param_count = yawn.get_device_param_count("instrument", 0)

    -- Line 1: parameter names (8 columns of 8 chars + separator)
    local names_line = ""
    local values_line = ""

    for i = 0, 7 do
        local col_width = 8
        local name = ""
        local val_str = ""

        if i < param_count then
            name = yawn.get_device_param_name("instrument", 0, i) or ""
            val_str = yawn.get_device_param_display("instrument", 0, i) or ""
        end

        -- Truncate and pad each column to col_width
        if #name > col_width then name = name:sub(1, col_width) end
        if #val_str > col_width then val_str = val_str:sub(1, col_width) end
        name = name .. string.rep(" ", col_width - #name)
        val_str = val_str .. string.rep(" ", col_width - #val_str)

        -- Add separator between columns (except after last)
        if i < 7 then
            names_line = names_line .. name .. " "
            values_line = values_line .. val_str .. " "
        else
            names_line = names_line .. name
            values_line = values_line .. val_str
        end
    end

    -- Line 3: track name
    local track_name = yawn.get_track_name(track) or ("Track " .. (track + 1))

    -- Line 4: instrument name
    local inst_name = yawn.get_instrument_name(track) or ""

    send_display_line(DISPLAY_LINES[1], names_line)
    send_display_line(DISPLAY_LINES[2], values_line)
    send_display_line(DISPLAY_LINES[3], track_name)
    send_display_line(DISPLAY_LINES[4], inst_name)
end

-- ── Pad Ripple Effect ───────────────────────────────────────────────────────

local function pad_to_grid(note)
    if note < PAD_GRID_START or note > PAD_GRID_END then return nil, nil end
    local idx = note - PAD_GRID_START
    return math.floor(idx / PAD_COLS), idx % PAD_COLS
end

local function grid_to_pad(row, col)
    if row < 0 or row > 7 or col < 0 or col > 7 then return nil end
    return PAD_GRID_START + row * PAD_COLS + col
end

local function set_pad_led(note, color)
    if note and note >= PAD_GRID_START and note <= PAD_GRID_END then
        yawn.midi_send(0x90, note, color)
    end
end

local function draw_ripple(row, col, radius, color)
    for dr = -radius, radius do
        for dc = -radius, radius do
            -- Only draw the ring at this radius, not filled
            if math.max(math.abs(dr), math.abs(dc)) == radius then
                local pad = grid_to_pad(row + dr, col + dc)
                if pad then set_pad_led(pad, color) end
            end
        end
    end
end

local function update_ripples()
    local still_active = {}
    for _, r in ipairs(ripples) do
        r.tick = r.tick + 1
        local row, col = r.row, r.col

        -- Clear previous ring
        if r.tick > 1 then
            draw_ripple(row, col, r.tick - 1, 0)
        end

        -- Draw current ring
        if r.tick <= #PAD_COLORS then
            draw_ripple(row, col, r.tick, PAD_COLORS[r.tick])
            still_active[#still_active + 1] = r
        end
    end
    ripples = still_active
end

-- ── Encoders ────────────────────────────────────────────────────────────────

local function handle_encoder(encoder_index, raw_value)
    -- Decode relative encoding: 1-63 = clockwise, 65-127 = counter-clockwise
    local delta = 0
    if raw_value >= 1 and raw_value <= 63 then
        delta = raw_value
    elseif raw_value >= 65 and raw_value <= 127 then
        delta = raw_value - 128  -- negative
    else
        return
    end

    local param_count = yawn.get_device_param_count("instrument", 0)
    local param_idx = encoder_index - 1  -- 0-based

    if param_idx >= param_count then return end

    local cur = yawn.get_device_param_value("instrument", 0, param_idx)
    local lo  = yawn.get_device_param_min("instrument", 0, param_idx)
    local hi  = yawn.get_device_param_max("instrument", 0, param_idx)
    local range = hi - lo
    if range <= 0 then return end

    local new_val = cur + delta * ENCODER_SENSITIVITY * range
    -- Clamp
    if new_val < lo then new_val = lo end
    if new_val > hi then new_val = hi end

    yawn.set_device_param("instrument", 0, param_idx, new_val)
    display_dirty = true
end

-- ── MIDI Callbacks ──────────────────────────────────────────────────────────

function on_midi(data)
    if #data < 1 then return end

    local status = data[1]
    local d1 = data[2] or 0
    local d2 = data[3] or 0
    local msg_type = status & 0xF0
    local channel = status & 0x0F

    -- CC on channel 0: check for encoder turns
    if msg_type == 0xB0 and channel == 0 then
        for i, enc_cc in ipairs(ENCODER_CCS) do
            if d1 == enc_cc then
                handle_encoder(i, d2)
                return
            end
        end
    end

    -- Note On / Note Off: forward pads to selected track, filter encoder touch
    if msg_type == 0x90 or msg_type == 0x80 then
        -- Filter encoder touch notes (0-7 on channel 0)
        if channel == 0 and ENCODER_TOUCH_NOTES[d1] then
            return  -- ignore encoder touch
        end

        -- Forward pad notes to the selected track
        local track = yawn.get_selected_track()
        local velocity = (msg_type == 0x90) and d2 or 0
        yawn.send_note_to_track(track, d1, velocity, channel)

        -- Visual feedback: light pad on press, start ripple
        if velocity > 0 then
            set_pad_led(d1, PAD_COLORS[1])
            local row, col = pad_to_grid(d1)
            if row then
                ripples[#ripples + 1] = {row = row, col = col, tick = 0}
            end
        else
            set_pad_led(d1, 0)
        end
        return
    end

    -- Filter polyphonic aftertouch (0xA0) - pads send this continuously
    if msg_type == 0xA0 then return end

    -- Log unhandled messages for debugging
    if #data >= 3 then
        yawn.log(string.format("MIDI: %02X %02X %02X", status, d1, d2))
    end
end

function on_tick()
    -- Check if track selection changed
    local track = yawn.get_selected_track()
    if track ~= last_selected_track then
        last_selected_track = track
        display_dirty = true
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

    -- Clear display first, then show welcome
    clear_display()
    send_display_line(DISPLAY_LINES[1], "                Y . A . W . N                   ")
    send_display_line(DISPLAY_LINES[2], "      Yet Another Audio Workstation New          ")
    send_display_line(DISPLAY_LINES[3], "")
    send_display_line(DISPLAY_LINES[4], "            Fasten your seatbelts!               ")

    display_dirty = true
    last_selected_track = yawn.get_selected_track()
end

function on_disconnect()
    yawn.log("Ableton Push 1 disconnected")
    clear_display()
end
