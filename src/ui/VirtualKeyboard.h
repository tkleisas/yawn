#pragma once

// VirtualKeyboard — maps QWERTY keys to MIDI note events.
// Layout matches Ableton Live: q2w3er5t6y7ui9o0p → C C# D D# E F F# G G# A A# B C C# D D# E
// Z = octave down, X = octave up. Default octave = 3 (middle C region).

#include "audio/AudioEngine.h"
#include "midi/MidiTypes.h"
#include <cstdint>
#include <unordered_map>

// SDL key codes
#include <SDL3/SDL_keycode.h>

namespace yawn {
namespace ui {

class VirtualKeyboard {
public:
    void init(audio::AudioEngine* engine) {
        m_engine = engine;
        buildKeyMap();
    }

    void setTargetTrack(int track) { m_targetTrack = track; }
    int targetTrack() const { return m_targetTrack; }

    void setOctave(int oct) { m_octave = (oct < 0) ? 0 : (oct > 8) ? 8 : oct; }
    int octave() const { return m_octave; }

    void setVelocity(uint16_t vel) { m_velocity = vel; }
    uint16_t velocity() const { return m_velocity; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }
    void toggle() { m_enabled = !m_enabled; }

    // Returns true if the key was consumed
    bool onKeyDown(SDL_Keycode key) {
        if (!m_enabled || !m_engine) return false;

        // Octave switching
        if (key == SDLK_Z) {
            if (m_octave > 0) m_octave--;
            return true;
        }
        if (key == SDLK_X) {
            if (m_octave < 8) m_octave++;
            return true;
        }

        auto it = m_keyMap.find(key);
        if (it == m_keyMap.end()) return false;

        int note = m_octave * 12 + it->second;
        if (note < 0 || note > 127) return false;

        // Don't retrigger if already held
        if (m_heldNotes[note]) return true;
        m_heldNotes[note] = true;

        m_engine->sendCommand(audio::SendMidiToTrackMsg{
            m_targetTrack,
            static_cast<uint8_t>(midi::MidiMessage::Type::NoteOn),
            0,                           // channel
            static_cast<uint8_t>(note),
            m_velocity,
            0
        });
        return true;
    }

    bool onKeyUp(SDL_Keycode key) {
        if (!m_enabled || !m_engine) return false;

        if (key == SDLK_Z || key == SDLK_X) return true;

        auto it = m_keyMap.find(key);
        if (it == m_keyMap.end()) return false;

        int note = m_octave * 12 + it->second;
        if (note < 0 || note > 127) return false;

        if (!m_heldNotes[note]) return true;
        m_heldNotes[note] = false;

        m_engine->sendCommand(audio::SendMidiToTrackMsg{
            m_targetTrack,
            static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
            0,
            static_cast<uint8_t>(note),
            0,
            0
        });
        return true;
    }

    // Release all held notes (e.g., when switching tracks or disabling)
    void allNotesOff() {
        if (!m_engine) return;
        for (int n = 0; n < 128; ++n) {
            if (m_heldNotes[n]) {
                m_heldNotes[n] = false;
                m_engine->sendCommand(audio::SendMidiToTrackMsg{
                    m_targetTrack,
                    static_cast<uint8_t>(midi::MidiMessage::Type::NoteOff),
                    0,
                    static_cast<uint8_t>(n),
                    0, 0
                });
            }
        }
    }

private:
    void buildKeyMap() {
        // Lower row: q 2 w 3 e r 5 t 6 y 7 u  → C..B (one octave)
        // Upper row: i 9 o 0 p                   → C..E (partial next octave)
        m_keyMap[SDLK_Q] = 0;   // C
        m_keyMap[SDLK_2] = 1;   // C#
        m_keyMap[SDLK_W] = 2;   // D
        m_keyMap[SDLK_3] = 3;   // D#
        m_keyMap[SDLK_E] = 4;   // E
        m_keyMap[SDLK_R] = 5;   // F
        m_keyMap[SDLK_5] = 6;   // F#
        m_keyMap[SDLK_T] = 7;   // G
        m_keyMap[SDLK_6] = 8;   // G#
        m_keyMap[SDLK_Y] = 9;   // A
        m_keyMap[SDLK_7] = 10;  // A#
        m_keyMap[SDLK_U] = 11;  // B
        m_keyMap[SDLK_I] = 12;  // C+1
        m_keyMap[SDLK_9] = 13;  // C#+1
        m_keyMap[SDLK_O] = 14;  // D+1
        m_keyMap[SDLK_0] = 15;  // D#+1
        m_keyMap[SDLK_P] = 16;  // E+1
    }

    audio::AudioEngine* m_engine = nullptr;
    int m_targetTrack = 0;
    int m_octave = 3;  // C3 = MIDI note 36
    uint16_t m_velocity = 100;
    bool m_enabled = true;
    bool m_heldNotes[128] = {};
    std::unordered_map<SDL_Keycode, int> m_keyMap;
};

} // namespace ui
} // namespace yawn
