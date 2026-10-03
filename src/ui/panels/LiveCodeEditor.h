#pragma once

// LiveCodeEditor — the fw2 face of the in-app Lua editor (phase 8).
// Wraps LiveCodeEditorKernel (pure text engine) with key-event
// mapping, a Lua tokenizer for syntax coloring, and direct-to-
// Renderer2D painting: line-number gutter, syntax-highlighted text,
// caret, and the completion popup. Hosted by LiveCodeConsole's Edit
// tab; the console forwards key events / text input while the tab is
// active. Ctrl+Enter evaluates the buffer via the onEvaluate callback
// (wired to LiveCodeManager::runScriptSource).

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

    // Host-forwarded key events (nav + commit keys; printable chars
    // arrive via textInput instead). Returns true when consumed.
    bool keyDown(fw2::Key key, bool ctrl, bool shift);

    // Host-forwarded SDL text input (printable characters).
    void textInput(const std::string& t) { m_k.insertText(t); }
    // Mouse: content-local coords → caret (col via pixel hit-test).
    // lineH/metrics come from the host (it has the UIContext).
    void click(float lx, float ly, float lineH, float fontSize,
               const fw2::TextMetrics& met,
               const ::yawn::ui::fw::Rect& contentRect);

    // Painting (caller provides the content rect and clips).
    void paint(fw2::UIContext& ctx, const ::yawn::ui::fw::Rect& r);

    // Buffer access (console loads the script file on tab switch).
    LiveCodeEditorKernel& kernel() { return m_k; }
    const LiveCodeEditorKernel& kernel() const { return m_k; }

private:
    LiveCodeEditorKernel m_k;
    int m_visibleLines = 20;      // set by paint; kept for hit-testing
};

} // namespace ui
} // namespace yawn
