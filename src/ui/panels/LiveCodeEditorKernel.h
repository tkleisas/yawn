#pragma once

// LiveCodeEditorKernel — the framework-free text engine behind the
// in-app Lua editor (phase 8). Owns the buffer (one string per line),
// the caret (line + UTF-8 byte column), horizontal-neutral navigation
// state, and the completion popup state machine (prefix filter over a
// static symbol table: Lua keywords + the yawn.*/improv.* API + common
// song-table keys).
//
// Pure std:: — no UI/framework deps — so tests drive it directly
// (tests/test_LiveCode.cpp). The fw2 wrapper (LiveCodeEditor.{h,cpp})
// maps key events onto these operations and does the rendering.

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {

class LiveCodeEditorKernel {
public:
    // ─── Buffer ──────────────────────────────────────────────────────
    void setText(const std::string& s) {
        m_lines.clear();
        std::string cur;
        for (char c : s) {
            if (c == '\n') { m_lines.push_back(cur); cur.clear(); }
            else           cur += c;
        }
        // A trailing newline terminates the last line — it doesn't
        // open a new (empty) one.
        if (!cur.empty() || m_lines.empty()) m_lines.push_back(cur);
        m_line = 0; m_col = 0; m_goalCol = 0; m_scrollY = 0.0f;
        m_modified = false;
        closeCompletion();
    }

    std::string text() const {
        std::string out;
        for (size_t i = 0; i < m_lines.size(); ++i) {
            if (i) out += '\n';
            out += m_lines[i];
        }
        return out;
    }

    const std::vector<std::string>& lines() const { return m_lines; }
    bool  modified() const { return m_modified; }
    void  clearModified()  { m_modified = false; }

    // ─── Caret ───────────────────────────────────────────────────────
    int caretLine() const { return m_line; }
    int caretCol()  const { return m_col; }   // UTF-8 byte offset

    void setScrollY(float y) { m_scrollY = y; }
    float scrollY() const    { return m_scrollY; }

    // ─── Editing ─────────────────────────────────────────────────────
    void insertText(const std::string& t) {
        if (!t.empty()) m_lines[m_line].insert(m_col, t);
        m_col += static_cast<int>(t.size());
        m_goalCol = m_col;
        m_modified = true;
        updateCompletion();
    }

    void backspace() {
        if (m_col > 0) {
            int back = 1;
            // Step over UTF-8 continuation bytes.
            while (m_col - back > 0 &&
                   (static_cast<unsigned char>(m_lines[m_line][m_col - back]) & 0xC0) == 0x80)
                ++back;
            m_lines[m_line].erase(m_col - back, back);
            m_col -= back;
        } else if (m_line > 0) {
            // Join with the previous line.
            m_col = static_cast<int>(m_lines[m_line - 1].size());
            m_lines[m_line - 1] += m_lines[m_line];
            m_lines.erase(m_lines.begin() + m_line);
            --m_line;
        }
        m_goalCol = m_col;
        m_modified = true;
        updateCompletion();
    }

    void deleteChar() {
        auto& line = m_lines[m_line];
        if (m_col < static_cast<int>(line.size())) {
            int fwd = 1;
            while (m_col + fwd < static_cast<int>(line.size()) &&
                   (static_cast<unsigned char>(line[m_col + fwd]) & 0xC0) == 0x80)
                ++fwd;
            line.erase(m_col, fwd);
        } else if (m_line + 1 < static_cast<int>(m_lines.size())) {
            line += m_lines[m_line + 1];
            m_lines.erase(m_lines.begin() + m_line + 1);
        }
        m_goalCol = m_col;
        m_modified = true;
        closeCompletion();
    }

    void splitLine() {
        auto& line = m_lines[m_line];
        const std::string tail = line.substr(m_col);
        line.resize(m_col);
        m_lines.insert(m_lines.begin() + m_line + 1, tail);
        ++m_line;
        m_col = 0;
        m_goalCol = 0;
        m_modified = true;
        closeCompletion();
    }

    // ─── Navigation ──────────────────────────────────────────────────
    void moveLeft() {
        if (m_col > 0) {
            --m_col;
            while (m_col > 0 &&
                   (static_cast<unsigned char>(m_lines[m_line][m_col]) & 0xC0) == 0x80)
                --m_col;
        } else if (m_line > 0) {
            --m_line;
            m_col = static_cast<int>(m_lines[m_line].size());
        }
        m_goalCol = m_col;
        closeCompletion();
    }

    void moveRight() {
        auto& line = m_lines[m_line];
        if (m_col < static_cast<int>(line.size())) {
            ++m_col;
            while (m_col < static_cast<int>(line.size()) &&
                   (static_cast<unsigned char>(line[m_col]) & 0xC0) == 0x80)
                ++m_col;
        } else if (m_line + 1 < static_cast<int>(m_lines.size())) {
            ++m_line;
            m_col = 0;
        }
        m_goalCol = m_col;
        closeCompletion();
    }

    void moveUp() {
        if (m_line > 0) {
            --m_line;
            m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        }
        closeCompletion();
    }

    void moveDown() {
        if (m_line + 1 < static_cast<int>(m_lines.size())) {
            ++m_line;
            m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        }
        closeCompletion();
    }

    void moveHome() { m_col = 0;     m_goalCol = m_col; closeCompletion(); }
    void moveEnd()  { m_col = static_cast<int>(m_lines[m_line].size()); m_goalCol = m_col; closeCompletion(); }

    // Mouse click → caret: line index directly; column estimated by
    // the caller from pixel hit-testing (pass a byte offset).
    void setCaret(int line, int col) {
        m_line = std::clamp(line, 0, static_cast<int>(m_lines.size()) - 1);
        m_col = std::clamp(col, 0, static_cast<int>(m_lines[m_line].size()));
        m_goalCol = m_col;
        closeCompletion();
    }

    // ─── Region/block evaluation (Ctrl+Shift+Enter) ──────────────────
    // A block is a maximal run of non-blank lines ("paragraphs" — blank
    // lines are the region separators, matching the demo-script style).
    // Range is [begin, end) — end EXCLUSIVE and file-clamped.
    std::pair<int, int> blockRange(int line) const {
        int lo = line, hi = line;
        if (lo < 0 || lo >= static_cast<int>(m_lines.size())) return {0, 0};
        while (lo > 0 && !trimCopy(m_lines[lo - 1]).empty()) --lo;
        while (hi < static_cast<int>(m_lines.size()) &&
               !trimCopy(m_lines[hi]).empty())
            ++hi;
        return {lo, lo == hi ? lo + 1 : hi};   // caret on a blank line
    }

    std::string blockTextAt(int line) const {
        const auto [lo, hi] = blockRange(line);
        std::string out;
        for (int i = lo; i < hi; ++i) {
            if (i > lo) out += '\n';
            out += m_lines[i];
        }
        return out;
    }

private:
    static std::string trimCopy(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
        return s.substr(b, e - b);
    }

public:
    // Word in front of the caret: [A-Za-z0-9_.] run (dotted prefixes
    // complete as a whole — "yawn.n" → "yawn.note").
    std::string completionPrefix() const {
        const std::string& line = m_lines[m_line];
        int start = m_col;
        while (start > 0) {
            const char c = line[start - 1];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
                break;
            --start;
        }
        return line.substr(start, m_col - start);
    }

    bool completionOpen() const { return m_completionOpen; }
    const std::vector<std::string>& completionItems() const { return m_items; }
    int completionSelected() const { return m_sel; }

    void selectNext() {
        if (!m_completionOpen || m_items.empty()) return;
        m_sel = std::min(m_sel + 1, static_cast<int>(m_items.size()) - 1);
    }
    void selectPrev() {
        if (!m_completionOpen) return;
        m_sel = std::max(m_sel - 1, 0);
    }

    // Accept the selected item: insert the remainder after the typed
    // prefix. Returns true when something was inserted.
    bool acceptCompletion() {
        if (!m_completionOpen || m_sel < 0 ||
            m_sel >= static_cast<int>(m_items.size()))
            return false;
        const std::string prefix = completionPrefix();
        const std::string& item = m_items[m_sel];
        if (item.size() > prefix.size() &&
            item.compare(0, prefix.size(), prefix) == 0) {
            insertRest(item.substr(prefix.size()));
        }
        closeCompletion();
        return true;
    }

    void closeCompletion() { m_completionOpen = false; m_items.clear(); m_sel = 0; }

    // Symbol table (static; the API surface of the live-coding layer).
    static const std::vector<std::string>& symbols() { return kSymbols; }

    void updateCompletion() {
        const std::string prefix = completionPrefix();
        m_items.clear();
        m_sel = 0;
        if (prefix.size() < 2) { m_completionOpen = false; return; }
        // Case-insensitive prefix filter.
        std::string lp = prefix;
        std::transform(lp.begin(), lp.end(), lp.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        for (const auto& sym : kSymbols) {
            std::string ls = sym;
            std::transform(ls.begin(), ls.end(), ls.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ls.compare(0, lp.size(), lp) == 0)
                m_items.push_back(sym);
        }
        if (m_items.empty() || m_items.size() > 64) {
            m_completionOpen = false;   // too broad → noise
            m_items.clear();
            return;
        }
        m_completionOpen = true;
    }

    void insertRest(const std::string& rest) { insertText(rest); }

private:
    std::vector<std::string> m_lines{""};
    int   m_line = 0, m_col = 0;
    int   m_goalCol = 0;        // column remembered for up/down moves
    float m_scrollY = 0.0f;
    bool  m_modified = false;

    bool m_completionOpen = false;
    std::vector<std::string> m_items;
    int  m_sel = 0;

    static const std::vector<std::string> kSymbols;
};

// Lua keywords first, then the yawn.* API, then improv.*, then common
// song-table keys (order = popup ordering on equal prefix).
inline const std::vector<std::string> LiveCodeEditorKernel::kSymbols = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "goto", "if", "in", "local", "nil", "not", "or",
    "repeat", "return", "then", "true", "until", "while",
    "yawn.log", "yawn.toast", "yawn.note", "yawn.note_off",
    "yawn.set_notes", "yawn.get_notes", "yawn.clear_notes",
    "yawn.midi", "yawn.drums",
    "yawn.new_midi_clip", "yawn.load_audio_file", "yawn.save_audio_buffer",
    "yawn.load_sample", "yawn.cancel_render", "yawn.is_playing",
    "yawn.set_playing", "yawn.get_bpm", "yawn.set_bpm", "yawn.get_beat",
    "yawn.get_bar", "yawn.state",
    "yawn.new_buffer", "yawn.buffer_data", "yawn.buffer_info",
    "yawn.free_buffer", "yawn.buffer_gain", "yawn.buffer_normalize",
    "yawn.buffer_fade", "yawn.buffer_mix", "yawn.buffer_reverse",
    "yawn.buffer_slice", "yawn.buffer_concat", "yawn.buffer_repeat",
    "yawn.buffer_mixdown", "yawn.fft", "yawn.ifft", "yawn.polyblep",
    "yawn.set_clip", "yawn.set_visual", "yawn.launch_scene",
    "yawn.launch_clip",
    "improv.every", "improv.after", "improv.at", "improv.on_bar",
    "improv.clear", "improv.clear_all", "improv.lookahead",
    "improv.late_policy",
    "song", "bpm", "scenes", "tracks", "name", "uid", "type", "volume",
    "mute", "solo", "instrument", "id", "params", "clips", "notes",
    "beats", "send", "sends", "pan",
};

} // namespace ui
} // namespace yawn
