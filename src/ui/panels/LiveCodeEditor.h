#pragma once

// LiveCodeEditor — the fw2 face of the in-app Lua editor (phase 8).
// Wraps LiveCodeEditorKernel (pure text engine) with key-event
// mapping, a Lua tokenizer for syntax coloring, and direct-to-
// Renderer2D painting: line-number gutter, syntax-highlighted text,
// caret, selection, error-line underline, and the completion popup.
// Hosted by LiveCodeConsole's Edit tab; the console forwards key
// events / text input while the tab is active. Ctrl+Enter evaluates
// the buffer via the onEvaluate callback (wired to
// LiveCodeManager::runScriptSource). Clipboard hooks are callbacks —
// the console bridges them to the app's SDL clipboard (the core is
// SDL-free); when unwired, an internal fallback buffer keeps
// Ctrl+X/C/V round-trips working (tests).
//
// The tokenizer result is cached per line; edits mark the first dirty
// line via the kernel's dirty tracking and painting re-tokenizes from
// there to the END (the long-comment state carries across lines).

#include "ui/framework/v2/Widget.h"      // fw2::Key, KeyEvent
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/panels/LiveCodeEditorKernel.h"

#include <functional>

namespace yawn {
namespace ui {
namespace fw {

struct Rect;   // v1 geometry (see framework/Rect.h)

} // namespace fw

class LiveCodeEditor {
public:
    // Ctrl+Enter → evaluate buffer (joined text).
    std::function<void(const std::string&)> onEvaluate;
    // Clipboard bridging (SDL lives in the app layer). Copy/cut hand
    // the selected text out; paste supplies the clipboard content.
    std::function<void(const std::string&)> onClipboardCopy;
    std::function<std::string()> onClipboardPaste;

    // Host-forwarded key events (nav + commit keys; printable chars
    // arrive via textInput instead). Returns true when consumed.
    bool keyDown(fw2::Key key, bool ctrl, bool shift);

    // Host-forwarded SDL text input (printable characters).
    void textInput(const std::string& t) {
        m_k.insertText(t);
        clearErrorLine();
    }
    // Mouse: content-local coords → caret (col via pixel hit-test).
    // lineH/metrics come from the host (it has the UIContext).
    // `extending` = shift-click (extends the selection).
    void click(float lx, float ly, float lineH, float fontSize,
               const fw2::TextMetrics& met,
               const ::yawn::ui::fw::Rect& contentRect,
               bool extending = false);

    // Drag-select gestures: start anchors at the press point, dragTo
    // extends the caret as the pointer sweeps. Both use the same
    // hit-test as click().
    void dragStart(float lx, float ly, float lineH, float fontSize,
                   const fw2::TextMetrics& met,
                   const ::yawn::ui::fw::Rect& contentRect);
    void dragTo(float lx, float ly, float lineH, float fontSize,
                const fw2::TextMetrics& met,
                const ::yawn::ui::fw::Rect& contentRect);

    // Double-click: select the word (or whitespace run) under the
    // press. Same coordinate contract as click().
    void doubleClick(float lx, float ly, float lineH, float fontSize,
                     const fw2::TextMetrics& met,
                     const ::yawn::ui::fw::Rect& contentRect);

    // Ends a drag-select sweep (mouseup). Clears the drag-active gate
    // so caret-following resumes.
    void endDrag();

    // Painting (caller provides the content rect and clips).
    void paint(fw2::UIContext& ctx, const ::yawn::ui::fw::Rect& r);

    // Buffer access (console loads the script file on tab switch).
    LiveCodeEditorKernel& kernel() { return m_k; }
    const LiveCodeEditorKernel& kernel() const { return m_k; }

    // Font zoom for the buffer only (multiplies fontSizeSmall).
    void  setFontScale(float s) { m_fontScale = s; }
    float fontScale() const     { return m_fontScale; }

    // Eval-error feedback: highlight a buffer line (0-based, -1 =
    // none). The console parses the Lua error chunk line number.
    void setErrorLine(int line0) {
        m_errorLine = (line0 >= 0 &&
                       line0 < static_cast<int>(m_k.lines().size()))
                          ? line0 : -1;
    }
    int errorLine() const { return m_errorLine; }

private:
    void clearErrorLine() { m_errorLine = -1; }
    // Shared click/drag caret math (content-local coords).
    void hitCaret(float lx, float ly, float lineH, float fontSize,
                  const fw2::TextMetrics& met,
                  const ::yawn::ui::fw::Rect& contentRect, bool extending);

    // Re-tokenize the dirty suffix (state-carrying long comments).
    void retokenize(const std::vector<std::string>& lines);

    LiveCodeEditorKernel m_k;
    int m_visibleLines = 20;      // set by paint; kept for hit-testing
    int m_errorLine = -1;         // eval-error row highlight (0-based)
    float m_fontScale = 1.0f;     // buffer zoom (A-/A+ buttons)
    bool  m_dragActive = false;   // sweep in progress: caret-follow gated off
    // Caret-follow bookkeeping: keep-in-view only runs when the caret
    // MOVED (scrollbar drags must not be re-centered by paint).
    int m_lastFollowLine = -1, m_lastFollowCol = -1;

    // Per-line token cache + the long-comment state AFTER each line
    // (so re-tokenizing from any line restarts with the right state).
    std::vector<std::vector<unsigned char>> m_toks;
    std::vector<bool> m_commentAfter;
};

} // namespace ui
} // namespace yawn
