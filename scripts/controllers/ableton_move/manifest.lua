return {
    name = "Ableton Move",
    author = "YAWN",
    version = "0.1",
    -- Exact port name depends on how the OS MIDI driver reports Move.
    -- ALSA reports "Ableton Move"; Windows WinMM reports
    -- "Ableton Move MIDI" (and several MIDIIN* variants) — the substring
    -- "Ableton Move" matches all of them. Adjust if your port shows up
    -- under a different name.
    input_port_match = "Ableton Move",
    output_port_match = "Ableton Move",
}
