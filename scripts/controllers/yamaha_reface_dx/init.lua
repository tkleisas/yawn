-- Yamaha Reface DX controller script for YAWN
--
-- The Reface DX is primarily an FM synthesizer with 37 mini keys,
-- a touch strip, and pitch bend. This script maps the available
-- controllers to DAW functions.
--
-- MIDI notes, pitch bend, and sustain are routed through YAWN's
-- standard MIDI engine (per-track MIDI input) — no script needed
-- for basic keyboard use.  This script handles the CC surface:
--
--   CC 1  (modulation / touch strip) → instrument param 0 of selected track
--   CC 11 (expression)               → track volume
--   CC 7  (volume)                   → master volume
--   CC 64 (sustain)                  → handled by MIDI engine natively
--
-- Touch-strip operation:
--   Left side (CC 1 ≈ 0)   → param min
--   Right side (CC 1 ≈ 127) → param max
--   The Reface touch strip rests at centre (~64) when released.

-- ── State ───────────────────────────────────────────────────────────────────

local selected_track = 0
local last_param_display = ""

-- ── Callbacks ───────────────────────────────────────────────────────────────

function on_connect()
    yawn.log("Reface DX connected")
    selected_track = yawn.get_selected_track() or 0
    yawn.toast("Reface DX ready", 1.0)
end

function on_disconnect()
    yawn.log("Reface DX disconnected")
end

function on_midi(msg)
    if #msg < 3 then return end

    local status = msg[1]
    local cc     = msg[2]
    local val    = msg[3]

    -- Only handle CC messages on channel 1 (0xB0)
    if status ~= 0xB0 then return end

    -- ── CC 1: Modulation / Touch Strip → instrument param 0 ──
    if cc == 1 then
        local t = yawn.get_selected_track()
        if t and t >= 0 then
            local count = yawn.get_device_param_count("instrument", 0)
            if count and count > 0 then
                local pmin = yawn.get_device_param_min("instrument", 0, 0)
                local pmax = yawn.get_device_param_max("instrument", 0, 0)
                local norm = val / 127.0
                local param_val = pmin + norm * (pmax - pmin)
                yawn.set_device_param("instrument", 0, 0, param_val)

                -- Show param display value on toast (rate-limited; only on change)
                local display = yawn.get_device_param_display("instrument", 0, 0)
                if display and display ~= last_param_display then
                    yawn.toast(display, 0.6)
                    last_param_display = display
                end
            end
        end
        return
    end

    -- ── CC 11: Expression → selected track volume ──
    if cc == 11 then
        local t = yawn.get_selected_track()
        if t and t >= 0 then
            local vol = val / 127.0 * 2.0
            yawn.set_track_volume(t, vol)
        end
        return
    end

    -- ── CC 7: Volume → master volume ──
    if cc == 7 then
        local vol = val / 127.0 * 2.0
        yawn.set_master_volume(vol)
        return
    end
end

function on_tick()
    -- No per-frame updates needed. MIDI notes are handled by the
    -- standard engine, and CC-based param changes are event-driven.
end
