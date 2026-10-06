#pragma once

// LiveCodeEditorKernel — the framework-free text engine behind the
// in-app Lua editor (phase 8). Owns the buffer (one string per line),
// the caret (line + UTF-8 byte column), horizontal-neutral navigation
// state, the selection (anchor + caret), an undo/redo stack
// (undo::UndoManager snapshots), horizontal scroll, and the completion
// popup state machine (prefix filter over a static symbol table).
//
// Pure std:: — no UI/framework deps — so tests drive it directly
// (tests/test_LiveCode.cpp). The fw2 wrapper (LiveCodeEditor.{h,cpp})
// maps key events onto these operations and does the rendering.
//
// Dirty-line tracking for the syntax tokenizer: every mutation bumps
// m_dirtyFrom (first affected line). The editor re-tokenizes from
// there to the END of the buffer (the long-comment state carries
// forward — a `--[[` opening above a dirty line still colors the
// rest), which makes edits cheap and full repaints bounded.

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "util/UndoManager.h"

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
        m_scrollX = 0.0f;
        clearSelection();
        m_modified = false;
        markDirty(0);
        m_undo.clear();
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

    // Horizontal scroll (pixels). The wrapper keeps the caret visible
    // and clamps; the kernel just owns the value.
    void setScrollX(float x) { m_scrollX = std::max(0.0f, x); }
    float scrollX() const    { return m_scrollX; }

    // ─── Selection ───────────────────────────────────────────────────
    bool hasSelection() const {
        if (m_anchorLine < 0) return false;
        return !(m_anchorLine == m_line && m_anchorCol == m_col);
    }
    // Normalized [start, end): start is the earliest (line,col).
    std::pair<std::pair<int,int>, std::pair<int,int>> selectionRange() const {
        if (m_anchorLine < m_line ||
            (m_anchorLine == m_line && m_anchorCol <= m_col))
            return {{m_anchorLine, m_anchorCol}, {m_line, m_col}};
        return {{m_line, m_col}, {m_anchorLine, m_anchorCol}};
    }

    void selectAll() {
        m_anchorLine = 0; m_anchorCol = 0;
        m_line = static_cast<int>(m_lines.size()) - 1;
        m_col  = static_cast<int>(m_lines[m_line].size());
        m_goalCol = m_col;
        closeCompletion();
    }

    // Selected text — lines joined with '\n' (paste-ready).
    std::string selectedText() const {
        if (!hasSelection()) return {};
        const auto [b, e] = selectionRange();
        if (b.first == e.first)
            return m_lines[b.first].substr(b.second, e.second - b.second);
        std::string out = m_lines[b.first].substr(b.second);
        for (int l = b.first + 1; l < e.first; ++l) { out += '\n'; out += m_lines[l]; }
        out += '\n';
        out += m_lines[e.first].substr(0, e.second);
        return out;
    }

    // ─── Editing ─────────────────────────────────────────────────────
    // Inserts at the caret (replacing an open selection). Supports
    // embedded '\n' — the paste path splits into real lines.
    void insertText(const std::string& t) {
        if (t.empty()) return;
        const int prevEditLine = m_editAnchorLine;
        const std::string id = coalesceId(Kind::Type, prevEditLine);
        const EditorState before = snapshot();
        if (hasSelection()) deleteSelectionOnly();
        const int firstLine = m_line;
        // First fragment lands on the caret's line; each '\n' spawns
        // a new line carrying the rest of the insert.
        size_t from = 0;
        int curLine = m_line;
        while (from <= t.size()) {
            const size_t nl = t.find('\n', from);
            const std::string piece = t.substr(from,
                nl == std::string::npos ? t.size() - from : nl - from);
            m_lines[curLine].insert(static_cast<size_t>(m_col), piece);
            m_col += static_cast<int>(piece.size());
            if (nl == std::string::npos) break;
            const std::string tail = m_lines[curLine].substr(
                static_cast<size_t>(m_col));
            m_lines[curLine].resize(static_cast<size_t>(m_col));
            m_lines.insert(m_lines.begin() + curLine + 1, tail);
            ++curLine;
            m_col = 0;
            from = nl + 1;
        }
        m_line = curLine;
        m_goalCol = m_col;
        m_editAnchorLine = firstLine;
        m_modified = true;
        markDirty(firstLine);
        commitUndo(before, id);
        updateCompletion();
    }

    void backspace() {
        if (hasSelection()) {
            cutRun();
            return;
        }
        const int prevEditLine = m_editAnchorLine;
        const std::string id = coalesceId(Kind::Backspace, prevEditLine);
        const EditorState before = snapshot();
        m_editAnchorLine = m_line;
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
        markDirty(m_line);
        commitUndo(before, id);
        updateCompletion();
    }

    void deleteChar() {
        if (hasSelection()) {
            cutRun();
            return;
        }
        const int prevEditLine = m_editAnchorLine;
        const std::string id = coalesceId(Kind::Delete, prevEditLine);
        const EditorState before = snapshot();
        m_editAnchorLine = m_line;
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
        markDirty(m_line);
        commitUndo(before, id);
        closeCompletion();
    }

    void splitLine() {
        const EditorState before = snapshot();
        auto& line = m_lines[m_line];
        const std::string tail = line.substr(m_col);
        line.resize(m_col);
        m_lines.insert(m_lines.begin() + m_line + 1, tail);
        ++m_line;
        m_col = 0;
        m_goalCol = 0;
        m_editAnchorLine = m_line - 1;
        breakRun();
        clearSelection();
        m_modified = true;
        markDirty(m_line - 1);
        commitUndo(before, {});
        closeCompletion();
    }

    // Selection ops (clipboard). copy() leaves the buffer alone; cut
    // deletes; paste replaces an open selection then inserts.
    std::string copySelection() {
        return selectedText();
    }

    std::string cutSelection() {
        if (!hasSelection()) return {};
        const std::string t = selectedText();
        const EditorState before = snapshot();
        const int first = selectionRange().first.first;
        deleteSelectionOnly();
        m_editAnchorLine = first;
        breakRun();
        m_modified = true;
        markDirty(first);
        commitUndo(before, {});
        closeCompletion();
        return t;
    }

    void pasteText(const std::string& t) {
        const EditorState before = snapshot();
        if (hasSelection()) deleteSelectionOnly();
        const int firstLine = m_line;
        // Split the paste into real lines at '\n'.
        size_t from = 0;
        int curLine = m_line;
        while (from <= t.size()) {
            const size_t nl = t.find('\n', from);
            const std::string piece = t.substr(from,
                nl == std::string::npos ? t.size() - from : nl - from);
            m_lines[curLine].insert(static_cast<size_t>(m_col), piece);
            m_col += static_cast<int>(piece.size());
            if (nl == std::string::npos) break;
            const std::string tail = m_lines[curLine].substr(
                static_cast<size_t>(m_col));
            m_lines[curLine].resize(static_cast<size_t>(m_col));
            m_lines.insert(m_lines.begin() + curLine + 1, tail);
            ++curLine;
            m_col = 0;
            from = nl + 1;
        }
        m_line = curLine;
        m_goalCol = m_col;
        m_editAnchorLine = firstLine;
        breakRun();
        m_modified = true;
        markDirty(firstLine);
        commitUndo(before, {});
        closeCompletion();
    }

    // ─── Navigation ──────────────────────────────────────────────────
    // `extend` = shift held: moves keep the anchor (start a selection
    // from the current caret if none is open).
    void moveLeft()  { collapseSel(); moveLeftImpl(); }
    void moveRight() { collapseSel(); moveRightImpl(); }
    void moveUp()    { collapseSel(); moveUpImpl(); }
    void moveDown()  { collapseSel(); moveDownImpl(); }
    void moveHome()  { collapseSel(); moveHomeImpl(); }
    void moveEnd()   { collapseSel(); moveEndImpl(); }

    void moveLeftExtend()  { beginExtend(); moveLeftImpl(); }
    void moveRightExtend() { beginExtend(); moveRightImpl(); }
    void moveUpExtend()    { beginExtend(); moveUpImpl(); }
    void moveDownExtend()  { beginExtend(); moveDownImpl(); }
    void moveHomeExtend()  { beginExtend(); moveHomeImpl(); }
    void moveEndExtend()   { beginExtend(); moveEndImpl(); }

    // Page moves: the wrapper passes the visible-line count.
    void movePageUp(int rows) {
        collapseSel();
        m_line = std::max(0, m_line - std::max(1, rows));
        m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        closeCompletion();
    }
    void movePageDown(int rows) {
        collapseSel();
        m_line = std::min(static_cast<int>(m_lines.size()) - 1,
                          m_line + std::max(1, rows));
        m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        closeCompletion();
    }
    // Ctrl+End / Ctrl+Start: buffer-end / buffer-start jumps.
    void moveBufferEnd() {
        collapseSel();
        m_line = static_cast<int>(m_lines.size()) - 1;
        m_col = m_goalCol = static_cast<int>(m_lines[m_line].size());
        closeCompletion();
    }
    void moveBufferStart() {
        collapseSel();
        m_line = 0; m_col = 0; m_goalCol = 0;
        closeCompletion();
    }

    // ─── Undo/redo — restore full (text, caret, anchor) snapshots ────
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    void undo() {
        if (!m_undo.canUndo()) return;
        m_undo.undo();
        markDirty(0);
        closeCompletion();
    }
    void redo() {
        if (!m_undo.canRedo()) return;
        m_undo.redo();
        markDirty(0);
        closeCompletion();
    }

    // ─── Dirty-line tracking (tokenizer cache) ───────────────────────
    int  dirtyFromLine() const { return m_dirtyFrom; }
    void clearDirty()          { m_dirtyFrom = kNoDirty; }

    // ─── Mouse click → caret ────────────────────────────────────────
    // Plain click collapses any selection; `extending` (shift-click)
    // extends from the anchor.
    void setCaret(int line, int col, bool extending = false) {
        line = std::clamp(line, 0, static_cast<int>(m_lines.size()) - 1);
        col  = std::clamp(col, 0, static_cast<int>(m_lines[line].size()));
        if (extending) {
            if (m_anchorLine < 0) { m_anchorLine = m_line; m_anchorCol = m_col; }
        } else {
            clearSelection();
        }
        m_line = line; m_col = col;
        m_goalCol = m_col;
        closeCompletion();
    }

    // Drag-select: mousedown anchors AT the press point, moves extend
    // (caret follows the pointer, anchor stays), mouseup just ends.
    void dragSelectStart(int line, int col) {
        setCaret(line, col, /*extending*/false);
        m_anchorLine = m_line; m_anchorCol = m_col;
    }
    void dragSelectTo(int line, int col) {
        setCaret(line, col, /*extending*/true);
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
    // ─── Undo plumbing ───────────────────────────────────────────────
    enum class Kind { None = 0, Type, Backspace, Delete };
    static constexpr int kNoDirty = 1 << 30;

    struct EditorState {
        std::vector<std::string> lines;   // exact lines (round-trips
                                          // trailing blank lines, which
                                          // the text() join does not)
        int line = 0, col = 0, goalCol = 0;
        int anchorLine = -1, anchorCol = 0;
    };

    EditorState snapshot() const {
        EditorState s;
        s.lines = m_lines;
        s.line = m_line; s.col = m_col; s.goalCol = m_goalCol;
        s.anchorLine = m_anchorLine; s.anchorCol = m_anchorCol;
        return s;
    }

    void restore(const EditorState& s) {
        m_lines = s.lines;
        if (m_lines.empty()) m_lines.push_back("");
        m_line = std::clamp(s.line, 0, static_cast<int>(m_lines.size()) - 1);
        m_col = std::clamp(s.col, 0, static_cast<int>(m_lines[m_line].size()));
        m_goalCol = std::clamp(s.goalCol, 0, static_cast<int>(m_lines[m_line].size()));
        m_anchorLine = std::min(s.anchorLine, static_cast<int>(m_lines.size()) - 1);
        m_anchorCol = std::clamp(s.anchorCol, 0,
            static_cast<int>(m_lines[std::max(0, m_anchorLine)].size()));
        if (m_anchorLine == m_line && m_anchorCol == m_col) m_anchorLine = -1;
        m_modified = true;
    }

    // Typing coalescing: consecutive same-kind edits on the same line
    // merge into one undo step; a line jump or kind switch starts a
    // new run (mergeId keyed per run).
    std::string coalesceId(Kind k, int prevEditLine) {
        if (m_lastKind != k || prevEditLine != m_line) ++m_run;
        m_lastKind = k;
        return std::string("edit") + std::to_string(m_run);
    }

    // Non-typing ops never coalesce and end any open run.
    void breakRun() { m_lastKind = Kind::None; ++m_run; }

    // Call AFTER the mutation: the redo closure captures the
    // post-edit snapshot, the undo closure the pre-edit one.
    void commitUndo(const EditorState& before, const std::string& mergeId) {
        undo::UndoEntry e;
        e.description = "edit";
        e.mergeId = mergeId;
        e.undoFn = [this, before]() { restore(before); };
        e.redoFn = [this, after = snapshot()]() { restore(after); };
        m_undo.push(std::move(e));
    }

    // Backspace/Delete over an open selection behaves like cut
    // (its own undo run).
    void cutRun() {
        const EditorState before = snapshot();
        const int first = selectionRange().first.first;
        deleteSelectionOnly();
        m_editAnchorLine = first;
        breakRun();
        m_modified = true;
        markDirty(first);
        commitUndo(before, {});
        closeCompletion();
    }

    void beginExtend() {
        if (m_anchorLine < 0) { m_anchorLine = m_line; m_anchorCol = m_col; }
    }
    void collapseSel() { m_anchorLine = -1; m_anchorCol = 0; }
    void clearSelection() { collapseSel(); }

    // Deletes the open selection, fixing the caret. Caller provides
    // the undo entry. `markDirty` is the CALLER's job (it knows the
    // first affected line).
    void deleteSelectionOnly() {
        const auto [b, e] = selectionRange();
        if (b.first == e.first) {
            m_lines[b.first].erase(static_cast<size_t>(b.second),
                                   static_cast<size_t>(e.second - b.second));
        } else {
            m_lines[b.first] =
                m_lines[b.first].substr(0, static_cast<size_t>(b.second)) +
                m_lines[e.first].substr(static_cast<size_t>(e.second));
            m_lines.erase(m_lines.begin() + b.first + 1,
                          m_lines.begin() + e.first + 1);
        }
        m_line = b.first; m_col = b.second;
        m_goalCol = m_col;
        clearSelection();
    }

    void markDirty(int line) {
        m_dirtyFrom = std::min(m_dirtyFrom, std::max(0, line));
    }

    // Move impls — shared by plain and extending variants.
    void moveLeftImpl() {
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

    void moveRightImpl() {
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

    void moveUpImpl() {
        if (m_line > 0) {
            --m_line;
            m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        }
        closeCompletion();
    }

    void moveDownImpl() {
        if (m_line + 1 < static_cast<int>(m_lines.size())) {
            ++m_line;
            m_col = std::min(m_goalCol, static_cast<int>(m_lines[m_line].size()));
        }
        closeCompletion();
    }

    void moveHomeImpl() { m_col = 0; m_goalCol = m_col; closeCompletion(); }
    void moveEndImpl()  { m_col = static_cast<int>(m_lines[m_line].size()); m_goalCol = m_col; closeCompletion(); }

    std::vector<std::string> m_lines{""};
    int   m_line = 0, m_col = 0;
    int   m_goalCol = 0;        // column remembered for up/down moves
    float m_scrollY = 0.0f;
    float m_scrollX = 0.0f;
    bool  m_modified = false;

    // Selection: anchor (line, byte col) or {-1,0} = collapsed.
    int   m_anchorLine = -1, m_anchorCol = 0;

    // Undo state.
    undo::UndoManager m_undo;
    Kind  m_lastKind = Kind::None;
    int   m_run = 0;
    int   m_editAnchorLine = 0;   // first buffer line the run touched

    // Tokenizer dirty tracking.
    int   m_dirtyFrom = 0;        // first line whose tokens are stale

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
