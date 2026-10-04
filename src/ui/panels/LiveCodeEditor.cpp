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
    switch (key) {
        case Key::Left:      m_k.moveLeft();    return true;
        case Key::Right:     m_k.moveRight();   return true;
        case Key::Up:
            if (m_k.completionOpen()) { m_k.selectPrev(); return true; }
            m_k.moveUp();                             return true;
        case Key::Down:
            if (m_k.completionOpen()) { m_k.selectNext(); return true; }
            m_k.moveDown();                           return true;
        case Key::Home:      m_k.moveHome();    return true;
        case Key::End:       m_k.moveEnd();     return true;
        case Key::Backspace: m_k.backspace();   return true;
        case Key::Delete:    m_k.deleteChar();  return true;
        case Key::Enter:
            if (m_k.completionOpen() && m_k.acceptCompletion()) return true;
            m_k.splitLine();                          return true;
        case Key::Tab:
            if (m_k.completionOpen() && m_k.acceptCompletion()) return true;
            m_k.insertText("    ");                   return true;
        case Key::Escape:
            if (m_k.completionOpen()) { m_k.closeCompletion(); return true; }
            return false;   // console closes itself
        default: return false;
    }
}


void LiveCodeEditor::click(float lx, float ly, float lineH, float fontSize,
                           const fw2::TextMetrics& met, const Rect& contentRect) {
    if (lineH <= 0.0f || contentRect.w <= 0.0f) return;
    const float gutterW = 34.0f;
    const float x0 = contentRect.x + gutterW;
    const int first = static_cast<int>(m_k.scrollY() / lineH);
    int line = first + static_cast<int>((ly - contentRect.y) / lineH);
    line = std::clamp(line, 0, static_cast<int>(m_k.lines().size()) - 1);
    const std::string& text = m_k.lines()[line];
    float x = lx - x0;
    int col = 0;
    while (col < static_cast<int>(text.size())) {
        int next = col + 1;
        while (next < static_cast<int>(text.size()) &&
               (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
            ++next;
        const float cw = met.textWidth(text.substr(col, next - col), fontSize);
        if (x < cw * 0.5f) break;
        x -= cw;
        col = next;
    }
    m_k.setCaret(line, col);
}

// ─── Painting ────────────────────────────────────────────────────────

void LiveCodeEditor::paint(fw2::UIContext& ctx, const Rect& r) {
    ::yawn::ui::Renderer2D& r2 = *ctx.renderer;
    if (!ctx.textMetrics) return;
    const auto& pal = theme().palette;
    const auto& met = theme().metrics;
    const float fontSize = met.fontSizeSmall;
    const fw2::TextMetrics& tm = *ctx.textMetrics;
    const float lineH = tm.lineHeight(fontSize);
    if (lineH <= 0.0f) return;

    const auto& lines = m_k.lines();
    const float gutterW = 34.0f;
    const float x0 = r.x + gutterW;
    const float areaH = r.h;

    // Keep the caret vertically in view (kernel owns the scroll).
    {
        const float caretY = static_cast<float>(m_k.caretLine()) * lineH;
        float sy = m_k.scrollY();
        if (caretY < sy) sy = caretY;
        if (caretY + lineH > sy + areaH)
            sy = caretY + lineH - areaH;
        m_k.setScrollY(std::max(0.0f, sy));
    }
    const float scrollY = m_k.scrollY();
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

    std::vector<std::vector<Tok>> toks(lines.size());
    bool longComment = false;
    for (size_t i = 0; i < lines.size(); ++i)
        tokenizeLine(lines[i], toks[i], longComment);

    const int end = std::min<int>(lines.size(), first + visible);
    for (int i = std::max(0, first); i < end; ++i) {
        const float y = r.y + static_cast<float>(i) * lineH - scrollY;
        const std::string& line = lines[i];

        char num[8];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        const float nw = tm.textWidth(num, fontSize);
        tm.drawText(r2, num, r.x + gutterW - nw - 6.0f, y, fontSize,
                    pal.textDim);

        const auto& tk = toks[i];
        float x = x0;
        size_t j = 0;
        while (j < line.size()) {
            size_t k = j;
            while (k < line.size() && tk[k] == tk[j]) ++k;
            const std::string chunk = line.substr(j, k - j);
            if (tk[j] != Tok::Normal)
                tm.drawText(r2, chunk, x, y, fontSize, tokColor(tk[j], pal));
            x += tm.textWidth(chunk, fontSize);
            j = k;
        }
        x = x0;
        j = 0;
        while (j < line.size()) {
            size_t k = j;
            while (k < line.size() && tk[k] == tk[j]) ++k;
            if (tk[j] == Tok::Normal) {
                const std::string chunk = line.substr(j, k - j);
                tm.drawText(r2, chunk, x, y, fontSize, pal.textPrimary);
            }
            x += tm.textWidth(line.substr(j, k - j), fontSize);
            j = k;
        }
    }

    // Caret.
    {
        const int cl = m_k.caretLine();
        if (cl >= first && cl < end) {
            const std::string& line = lines[cl];
            const float cw = tm.textWidth(
                line.substr(0, std::min<size_t>(m_k.caretCol(), line.size())),
                fontSize);
            const float cy = r.y + static_cast<float>(cl) * lineH - scrollY;
            r2.drawRect(x0 + cw, cy + 1.0f, 1.5f, lineH - 2.0f, pal.accent);
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
        const float px = x0 + tm.textWidth(m_k.completionPrefix(), fontSize);
        r2.drawRect(px, py, boxW, boxH, pal.elevated.withAlpha(244));
        r2.drawRectOutline(px, py, boxW, boxH, pal.border);
        for (int n = 0; n < rows; ++n) {
            const bool sel = (n == m_k.completionSelected());
            if (sel)
                r2.drawRect(px + 1.0f, py + 3.0f + n * rowH, boxW - 2.0f,
                            rowH, pal.accent.withAlpha(40));
            tm.drawText(r2, items[n], px + 6.0f, py + 3.0f + n * rowH,
                        fontSize, sel ? pal.accent : pal.textPrimary);
        }
    }
}

} // namespace ui
} // namespace yawn
