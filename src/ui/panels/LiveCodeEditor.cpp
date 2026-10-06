#include "ui/panels/LiveCodeEditor.h"

#include "ui/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace ui {

using namespace ::yawn::ui::fw;
using fw2::Key;
using fw2::UIContext;
using fw2::theme;

namespace {

// Lua token classes → palette (dark-theme tuned; coherent with the
// app's Theme but distinct hues so classes read at a glance).
enum class Tok : uint8_t { Normal, Keyword, String, Number, Comment, Api };

Color tokColor(Tok t, const fw2::ThemePalette& pal) {
    switch (t) {
        case Tok::Keyword: return {255, 122, 178};   // pink
        case Tok::String:  return {255, 212, 121};   // amber
        case Tok::Number:  return {122, 224, 255};   // cyan
        case Tok::Comment: return {107, 122, 143};   // slate
        case Tok::Api:     return {179, 255, 122};   // green
        default:           return pal.textPrimary;
    }
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

// One line → token classes (same length as the line). `inLongComment`
// carries the --[[ ]] state across lines.
void tokenizeLine(const std::string& line, std::vector<Tok>& out,
                  bool& inLongComment) {
    out.assign(line.size(), Tok::Normal);
    const std::string keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    };
    size_t i = 0;
    std::string prevIdent;             // last identifier word seen
    while (i < line.size()) {
        if (inLongComment) {
            const size_t close = line.find("]]", i);
            if (close == std::string::npos) {
                std::fill(out.begin() + i, out.end(), Tok::Comment);
                return;                // still inside on the next line
            }
            std::fill(out.begin() + i, out.begin() + close + 2, Tok::Comment);
            i = close + 2;
            inLongComment = false;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '-' && line[i + 1] == '-') {
            if (i + 3 < line.size() && line.compare(i + 2, 2, "[[") == 0) {
                inLongComment = true;
                const size_t close = line.find("]]", i + 4);
                if (close == std::string::npos) {
                    std::fill(out.begin() + i, out.end(), Tok::Comment);
                    return;            // long comment continues next line
                }
                std::fill(out.begin() + i, out.begin() + close + 2, Tok::Comment);
                i = close + 2;
                inLongComment = false;
            } else {
                std::fill(out.begin() + i, out.end(), Tok::Comment);
                return;
            }
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            size_t j = i + 1;
            while (j < line.size() && line[j] != quote) {
                if (line[j] == '\\' && j + 1 < line.size()) ++j;
                ++j;
            }
            const size_t end = std::min(j + 1, line.size());
            std::fill(out.begin() + i, out.begin() + end, Tok::String);
            i = end;
            continue;
        }
        if (isDigit(line[i]) ||
            (line[i] == '.' && i + 1 < line.size() && isDigit(line[i + 1]))) {
            size_t j = i;
            while (j < line.size() && (isIdentChar(line[j]) || line[j] == '.'))
                ++j;
            // Allow one exponent sign directly after e/E.
            if (j > i && j < line.size() &&
                (std::tolower(line[j - 1]) == 'e') &&
                (line[j] == '+' || line[j] == '-'))
                ++j;
            std::fill(out.begin() + i, out.begin() + j, Tok::Number);
            i = j;
            continue;
        }
        if (isIdentStart(line[i])) {
            size_t j = i;
            while (j < line.size() && isIdentChar(line[j])) ++j;
            const std::string word = line.substr(i, j - i);
            const bool dottedApi =
                (prevIdent == "yawn" || prevIdent == "improv" || prevIdent == "song");
            Tok t = Tok::Normal;
            if (dottedApi)            t = Tok::Api;
            else if (prevIdent.empty() || prevIdent != ".") {
                for (const auto& kw : keywords)
                    if (kw == word) { t = Tok::Keyword; break; }
                if (word == "yawn" || word == "improv") t = Tok::Api;
            }
            std::fill(out.begin() + i, out.begin() + j, t);
            prevIdent = word;
            i = j;
            continue;
        }
        if (line[i] == '.') prevIdent = ".";   // dotted-name bookkeeping
        else if (line[i] != ':' && !std::isspace(line[i])) prevIdent.clear();
        ++i;
    }
}

float tokenRunWidth(const fw2::TextMetrics& met, float fontSize,
                    const std::string& text, size_t from, size_t to) {
    return met.textWidth(text.substr(from, to - from), fontSize);
}

} // namespace
// ─── Input ───────────────────────────────────────────────────────────

bool LiveCodeEditor::keyDown(fw2::Key key, bool ctrl, bool shift) {
    if (ctrl && key == Key::Enter) {
        if (!onEvaluate) return true;
        // Ctrl+Shift+Enter = run the REGION at the caret (blank-line
        // delimited block — the script's structural units); Ctrl+Enter
        // evaluates the whole buffer.
        if (shift) onEvaluate(m_k.blockTextAt(m_k.caretLine()));
        else       onEvaluate(m_k.text());
        return true;
    }
    if (ctrl) {
        switch (key) {
            case Key::Z: m_k.undo(); clearErrorLine(); return true;
            case Key::Y: m_k.redo(); clearErrorLine(); return true;
            case Key::A: m_k.selectAll();              return true;
            case Key::End:      m_k.moveBufferEnd(); clearErrorLine(); return true;
            case Key::Home:     m_k.moveBufferStart(); clearErrorLine(); return true;
            case Key::C: {
                const std::string t = m_k.copySelection();
                if (!t.empty() && onClipboardCopy) onClipboardCopy(t);
                return true;
            }
            case Key::X: {
                const std::string t = m_k.cutSelection();
                if (!t.empty() && onClipboardCopy) onClipboardCopy(t);
                clearErrorLine();                       return true;
            }
            case Key::V: {
                if (onClipboardPaste) {
                    const std::string t = onClipboardPaste();
                    if (!t.empty()) { m_k.pasteText(t); clearErrorLine(); }
                }
                return true;
            }
            default: break;
        }
    }
    switch (key) {
        case Key::Left:
            if (shift) m_k.moveLeftExtend(); else m_k.moveLeft();
            clearErrorLine();                             return true;
        case Key::Right:
            if (shift) m_k.moveRightExtend(); else m_k.moveRight();
            clearErrorLine();                             return true;
        case Key::Up:
            if (m_k.completionOpen()) { m_k.selectPrev(); return true; }
            if (shift) m_k.moveUpExtend(); else m_k.moveUp();
            clearErrorLine();                             return true;
        case Key::Down:
            if (m_k.completionOpen()) { m_k.selectNext(); return true; }
            if (shift) m_k.moveDownExtend(); else m_k.moveDown();
            clearErrorLine();                             return true;
        case Key::Home:
            if (shift) m_k.moveHomeExtend(); else m_k.moveHome();
            clearErrorLine();                             return true;
        case Key::End:
            if (shift) m_k.moveEndExtend(); else m_k.moveEnd();
            clearErrorLine();                             return true;
        case Key::PageUp:   m_k.movePageUp(m_visibleLines); clearErrorLine(); return true;
        case Key::PageDown: m_k.movePageDown(m_visibleLines); clearErrorLine(); return true;
        case Key::Backspace: m_k.backspace(); clearErrorLine(); return true;
        case Key::Delete:    m_k.deleteChar(); clearErrorLine(); return true;
        case Key::Enter:
            if (m_k.completionOpen() && m_k.acceptCompletion()) return true;
            m_k.splitLine(); clearErrorLine();            return true;
        case Key::Tab:
            if (m_k.completionOpen() && m_k.acceptCompletion()) return true;
            m_k.insertText("    "); clearErrorLine();     return true;
        case Key::Escape:
            if (m_k.completionOpen()) { m_k.closeCompletion(); return true; }
            return false;   // console closes itself
        default: break;
    }
    // Printable keys are CONSUMED here but inserted via the host's
    // TEXT_INPUT path (when SDL text input is active both events fire
    // for one press). Without the consume, Space/letters fall through
    // to the app's global shortcuts while typing in the buffer —
    // space toggled the transport mid-sentence.
    using U = int;
    const U k = static_cast<U>(key);
    if (key == Key::Space ||
        (k >= U(Key::A) && k <= U(Key::Z)) ||
        (k >= U(Key::Num0) && k <= U(Key::Num9)))
        return true;
    return false;
}


void LiveCodeEditor::click(float lx, float ly, float lineH, float fontSize,
                           const fw2::TextMetrics& met,
                           const fw::Rect& contentRect, bool extending) {
    if (lineH <= 0.0f || contentRect.w <= 0.0f) return;
    hitCaret(lx, ly, lineH, fontSize, met, contentRect, extending);
    clearErrorLine();
}

void LiveCodeEditor::dragStart(float lx, float ly, float lineH,
                               float fontSize, const fw2::TextMetrics& met,
                               const fw::Rect& contentRect) {
    if (lineH <= 0.0f || contentRect.w <= 0.0f) return;
    hitCaret(lx, ly, lineH, fontSize, met, contentRect, /*extending*/false);
    m_k.dragSelectStart(m_k.caretLine(), m_k.caretCol());
    clearErrorLine();
}

void LiveCodeEditor::dragTo(float lx, float ly, float lineH, float fontSize,
                            const fw2::TextMetrics& met,
                            const fw::Rect& contentRect) {
    if (lineH <= 0.0f || contentRect.w <= 0.0f) return;
    hitCaret(lx, ly, lineH, fontSize, met, contentRect, /*extending*/true);
    // Sweep auto-scroll: past the view's edges, scroll by the
    // overshoot so rows beyond the visible band stay reachable (the
    // moves arrive per-frame; the caret-follow handles the rest).
    const float maxScroll = std::max(
        0.0f, static_cast<float>(m_k.lines().size()) * lineH - contentRect.h);
    float sy = m_k.scrollY();
    if (ly < contentRect.y)
        sy -= (contentRect.y - ly);
    else if (ly > contentRect.y + contentRect.h)
        sy += (ly - contentRect.y - contentRect.h);
    m_k.setScrollY(std::clamp(sy, 0.0f, maxScroll));
}

void LiveCodeEditor::doubleClick(float lx, float ly, float lineH,
                                 float fontSize, const fw2::TextMetrics& met,
                                 const fw::Rect& contentRect) {
    if (lineH <= 0.0f || contentRect.w <= 0.0f) return;
    const float gutterW = 34.0f;
    const int first = static_cast<int>(m_k.scrollY() / lineH);
    int line = first + static_cast<int>((ly - contentRect.y) / lineH);
    line = std::clamp(line, 0, static_cast<int>(m_k.lines().size()) - 1);
    const std::string& text = m_k.lines()[line];
    const int col = met.byteOffsetAtX(text, fontSize,
                                      lx - (contentRect.x + gutterW) + m_k.scrollX());
    m_k.selectWordAt(line, col);
    clearErrorLine();
}

void LiveCodeEditor::hitCaret(float lx, float ly, float lineH,
                              float fontSize, const fw2::TextMetrics& met,
                              const fw::Rect& contentRect, bool extending) {    const float gutterW = 34.0f;
    const float x0 = contentRect.x + gutterW;
    const int first = static_cast<int>(m_k.scrollY() / lineH);
    int line = first + static_cast<int>((ly - contentRect.y) / lineH);
    line = std::clamp(line, 0, static_cast<int>(m_k.lines().size()) - 1);
    const std::string& text = m_k.lines()[line];
    // Account for horizontal scroll: the buffer is drawn shifted left.
    const int col = met.byteOffsetAtX(text, fontSize, lx - x0 + m_k.scrollX());
    m_k.setCaret(line, col, extending);
}

// ─── Painting ────────────────────────────────────────────────────────

// Re-tokenize the dirty suffix. Tokens live per line; the long-comment
// state after line L is cached so restarting at the first dirty line
// continues with the correct comment context. `dirtyFrom` <= 0 means
// the whole buffer is stale (load / undo / first paint).
void LiveCodeEditor::retokenize(const std::vector<std::string>& lines) {
    const int n = static_cast<int>(lines.size());
    const int dirty = std::clamp(m_k.dirtyFromLine(), 0, n);
    const int from = (static_cast<int>(m_toks.size()) != n ||
                      m_commentAfter.size() != static_cast<size_t>(n))
                         ? 0 : dirty;
    bool longComment = false;
    if (from > 0 && from < static_cast<int>(m_commentAfter.size()))
        longComment = m_commentAfter[static_cast<size_t>(from) - 1];
    m_toks.resize(static_cast<size_t>(n));
    m_commentAfter.resize(static_cast<size_t>(n));
    for (int i = from; i < n; ++i) {
        std::vector<Tok> tk;
        tokenizeLine(lines[i], tk, longComment);
        m_toks[static_cast<size_t>(i)].resize(tk.size());
        for (size_t j = 0; j < tk.size(); ++j)
            m_toks[static_cast<size_t>(i)][j] =
                static_cast<unsigned char>(tk[j]);
        m_commentAfter[static_cast<size_t>(i)] = longComment;
    }
    m_k.clearDirty();
}

void LiveCodeEditor::paint(fw2::UIContext& ctx, const Rect& r) {
    ::yawn::ui::Renderer2D& r2 = *ctx.renderer;
    if (!ctx.textMetrics) return;
    const auto& pal = theme().palette;
    const auto& met = theme().metrics;
    const float fontSize = met.fontSizeSmall * m_fontScale;
    const fw2::TextMetrics& tm = *ctx.textMetrics;
    const float lineH = tm.lineHeight(fontSize);
    if (lineH <= 0.0f) return;

    const auto& lines = m_k.lines();
    const float gutterW = 34.0f;
    const float x0 = r.x + gutterW;
    const float areaH = r.h;

    // Keep the caret in view — but ONLY when the caret moved since the
    // last paint. The scrollbar drag mutates scrollY directly; a
    // per-frame re-clamp to the caret's line yanked the view back the
    // moment the drag crossed the caret's edge (drag 100-200px, then
    // "it stops"). Caret moves (typing, navigation, clicks, sweep
    // drags) still follow; pure scroll changes never re-center.
    {
        const int cl = m_k.caretLine();
        const int cc = m_k.caretCol();
        const bool caretMoved =
            m_lastFollowLine < 0 || cl != m_lastFollowLine ||
            cc != m_lastFollowCol;
        if (caretMoved) {
            m_lastFollowLine = cl;
            m_lastFollowCol  = cc;
            const float caretY = static_cast<float>(cl) * lineH;
            float sy = m_k.scrollY();
            if (caretY < sy) sy = caretY;
            if (caretY + lineH > sy + areaH)
                sy = caretY + lineH - areaH;
            m_k.setScrollY(std::max(0.0f, sy));

            // Horizontal: caret x vs the view width.
            const std::string& line = lines[cl];
            const float caretX = tm.textWidth(line.substr(
                0, std::min<size_t>(cc, line.size())), fontSize);
            const float viewW = r.w - gutterW - 6.0f;   // small right margin
            float sx = m_k.scrollX();
            if (caretX - sx > viewW) sx = caretX - viewW;
            if (caretX - 12.0f < sx) sx = std::max(0.0f, caretX - 12.0f);
            const float maxSx = std::max(0.0f, tm.textWidth(line, fontSize) - viewW);
            m_k.setScrollX(std::clamp(sx, 0.0f, maxSx));
        }
    }
    const float scrollY = m_k.scrollY();
    const float scrollX = m_k.scrollX();
    const int first = static_cast<int>(scrollY / lineH);
    const int visible = static_cast<int>(areaH / lineH) + 1;
    m_visibleLines = visible;

    r2.drawRect(r.x, r.y, gutterW, r.h, pal.background);

    // Region indicators: one subtle pill per block (blank-line runs) on
    // the gutter's left edge; the caret's block is highlighted.
    {
        const auto [caretLo, caretHi] = m_k.blockRange(m_k.caretLine());
        int blockStart = 0;
        int blockIdx = 0;
        for (int l = 0; l <= static_cast<int>(lines.size()); ++l) {
            const bool end = (l == static_cast<int>(lines.size())) ||
                             m_k.lines()[l].empty();
            if (end && l > blockStart) {
                const bool isCaret = (caretLo >= blockStart && caretLo < l);
                const Color c = isCaret ? pal.accent : pal.borderSubtle;
                r2.drawRect(r.x + 2.0f, r.y + static_cast<float>(blockStart)
                                * lineH - scrollY + 2.0f,
                            3.0f, static_cast<float>(l - blockStart) * lineH
                                - 4.0f, c);
                ++blockIdx;
            }
            if (end) blockStart = l + 1;
        }
        (void)blockIdx;
    }

    // Cached per-line tokens (dirty-suffix re-tokenize).
    retokenize(lines);

    const int end = std::min<int>(lines.size(), first + visible);

    // Error line tint (eval feedback).
    const int errLine = m_errorLine;
    if (errLine >= first && errLine < end)
        r2.drawRect(r.x + gutterW,
                    r.y + static_cast<float>(errLine) * lineH - scrollY,
                    r.w - gutterW, lineH, {200, 60, 50, 42});

    r2.pushClip(r.x, r.y, r.w, r.h);
    for (int i = std::max(0, first); i < end; ++i) {
        const float y = r.y + static_cast<float>(i) * lineH - scrollY;
        // v1 Font::drawText renders glyphs ~0.15 line-height BELOW the
        // y param (the stbtt baseline offset every other widget shims
        // away with its centering math). Compensate here so the glyphs
        // visually CENTER in the row band the hit-test uses — without
        // this, big font zooms push the ink past the band's midpoint
        // and word-selection clicks must aim above the word.
        const float ty = y - lineH * 0.15f;
        const std::string& line = lines[i];

        char num[8];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        const float nw = tm.textWidth(num, fontSize);
        tm.drawText(r2, num, r.x + gutterW - nw - 6.0f, ty, fontSize,
                    pal.textDim);

        const auto& tk = m_toks[static_cast<size_t>(i)];
        // Selection highlight (under the glyphs). Per-line span:
        // start at the anchor edge (or the line start for middle
        // lines), end at the caret edge (or the line end).
        if (m_k.hasSelection()) {
            const auto [b, e] = m_k.selectionRange();
            if (i >= b.first && i <= e.first) {
                const bool same = (b.first == e.first);
                if (same ? (i == b.first) : (i > b.first && i < e.first ||
                                             i == b.first || i == e.first)) {
                    const size_t s0 = (i == b.first)
                                         ? static_cast<size_t>(b.second) : 0;
                    const size_t s1 = (i == e.first)
                                         ? static_cast<size_t>(e.second)
                                         : line.size();
                    if (s1 > s0) {
                        const float sx0 =
                            x0 - scrollX + tm.textWidth(
                                line.substr(0, s0), fontSize);
                        const float sw = tm.textWidth(
                            line.substr(s0, s1 - s0), fontSize);
                        r2.drawRect(sx0, y, sw, lineH,
                                    pal.accent.withAlpha(56));
                    } else if (s1 == s0 && b.first != e.first && line.empty()) {
                        // selected empty line inside a block selection
                        r2.drawRect(x0 - scrollX, y, 3.0f, lineH,
                                    pal.accent.withAlpha(56));
                    }
                }
            }
        }
        float x = x0 - scrollX;
        size_t j = 0;
        while (j < line.size()) {
            size_t k = j;
            while (k < line.size() && tk[k] == tk[j]) ++k;
            const std::string chunk = line.substr(j, k - j);
            const Color c = static_cast<Tok>(tk[j]) == Tok::Normal
                                ? pal.textPrimary
                                : tokColor(static_cast<Tok>(tk[j]), pal);
            tm.drawText(r2, chunk, x, ty, fontSize, c);
            x += tm.textWidth(chunk, fontSize);
            j = k;
        }
    }
    r2.popClip();

    // Caret.
    {
        const int cl = m_k.caretLine();
        if (cl >= first && cl < end) {
            const std::string& line = lines[cl];
            const float cw = tm.textWidth(
                line.substr(0, std::min<size_t>(m_k.caretCol(), line.size())),
                fontSize);
            const float cy = r.y + static_cast<float>(cl) * lineH - scrollY;
            r2.drawRect(x0 + cw - scrollX, cy + 1.0f, 1.5f, lineH - 2.0f,
                        pal.accent);
        }
    }

    // Completion popup.
    if (m_k.completionOpen() && !m_k.completionItems().empty()) {
        const auto& items = m_k.completionItems();
        const int rows = std::min<int>(8, static_cast<int>(items.size()));
        const float rowH = lineH;
        const float boxW = 220.0f;
        const float boxH = rows * rowH + 6.0f;
        float py = r.y + static_cast<float>(m_k.caretLine() + 1) * lineH
                   - scrollY + 2.0f;
        if (py + boxH > r.y + r.h)
            py = r.y + static_cast<float>(m_k.caretLine()) * lineH
                 - scrollY - boxH - 2.0f;
        const float px = x0 - scrollX
                       + tm.textWidth(m_k.completionPrefix(), fontSize);
        r2.drawRect(px, py, boxW, boxH, pal.elevated.withAlpha(244));
        r2.drawRectOutline(px, py, boxW, boxH, pal.border);
        for (int n = 0; n < rows; ++n) {
            const bool sel = (n == m_k.completionSelected());
            if (sel)
                r2.drawRect(px + 1.0f, py + 3.0f + n * rowH, boxW - 2.0f,
                            rowH, pal.accent.withAlpha(40));
            tm.drawText(r2, items[n], px + 6.0f,
                        py + 3.0f + n * rowH - rowH * 0.15f,
                        fontSize, sel ? pal.accent : pal.textPrimary);
        }
    }
}

} // namespace ui
} // namespace yawn
