#include "ui/panels/LiveCodeConsole.h"
#include "livecode/LiveCodeManager.h"
#include "livecode/LiveCodeSong.h"
#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Renderer.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace yawn {
namespace ui {

using fw2::OverlayLayer;
using fw2::OverlayEntry;
using fw2::theme;
using namespace ::yawn::ui::fw;

namespace {

// Layout (logical px)
constexpr float kPanelW = 760.0f;
constexpr float kPanelH = 400.0f;
constexpr float kHeaderH = 30.0f;
constexpr float kTabH = 26.0f;
constexpr float kTabW = 70.0f;
constexpr float kBtnW = 82.0f;
constexpr float kBtnH = 22.0f;
constexpr float kPad = 8.0f;

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else           cur += c;
    }
    out.push_back(cur);
    return out;
}

enum class Zone : uint8_t { None, RunStop, Reload, Clear, Save, TabLog, TabCode, TabEdit };

Zone zoneAt(const Rect& panel, float lx, float ly) {
    // Buttons: header right side (Save appears left of Run in the
    // Edit tab only).
    if (ly >= 4.0f && ly < 4.0f + kBtnH && panel.w > kPad + 4 * (kBtnW + 6.0f)) {
        float x = panel.w - kPad - kBtnW;              // rightmost = Clear
        if (lx >= x && lx < x + kBtnW) return Zone::Clear;
        x -= kBtnW + 6.0f;                             // Reload
        if (lx >= x && lx < x + kBtnW) return Zone::Reload;
        x -= kBtnW + 6.0f;                             // Run / Stop
        if (lx >= x && lx < x + kBtnW) return Zone::RunStop;
        x -= kBtnW + 6.0f;                             // Save (Edit tab)
        if (lx >= x && lx < x + kBtnW) return Zone::Save;
    }
    // Tabs: row under the header.
    const float tabY = kHeaderH + 2.0f;
    if (ly >= tabY && ly < tabY + kTabH) {
        if (lx >= kPad && lx < kPad + kTabW) return Zone::TabLog;
        if (lx >= kPad + kTabW + 6.0f &&
            lx < kPad + 2.0f * kTabW + 6.0f) return Zone::TabCode;
        if (lx >= kPad + 2.0f * (kTabW + 6.0f) &&
            lx < kPad + 3.0f * kTabW + 2.0f * 6.0f) return Zone::TabEdit;
    }
    return Zone::None;
}

} // namespace

LiveCodeConsole::LiveCodeConsole() {
    m_editor.onEvaluate = [this](const std::string& code) {
        if (m_mgr) m_mgr->runScriptSource(code);
    };
}

void LiveCodeConsole::toggle(fw2::UIContext& ctx) {
    if (isOpen()) close();
    else          pushOverlay(ctx);
}

void LiveCodeConsole::close() {
    m_handle.remove();
    m_scroll = 0.0f;
}

void LiveCodeConsole::pushOverlay(fw2::UIContext& ctx) {
    if (!ctx.layerStack || !m_mgr) return;
    refreshCodeLens();
    m_scroll = 0.0f;

    const float vw = ctx.viewport.w, vh = ctx.viewport.h;
    const float w = std::min(kPanelW, std::max(340.0f, vw - 32.0f));
    const float h = std::min(kPanelH, std::max(220.0f, vh * 0.55f));
    m_panel = Rect{16.0f, 8.0f, w, h};

    OverlayEntry entry;
    entry.debugName = "LiveCodeConsole";
    entry.ariaLabel = "Live code console";
    entry.bounds         = m_panel;
    entry.modal          = false;
    entry.dismissOnOutsideClick = false;
    entry.paint = [this, panel = m_panel](fw2::UIContext& c) {
        paintBody(c, panel);
    };
    entry.onMouseDown = [this, panel = m_panel](fw2::MouseEvent& e) {
        return handleMouseDown(e, panel);
    };
    entry.onMouseUp   = [this, panel = m_panel](fw2::MouseEvent& e) {
        return panel.contains(e.x, e.y);
    };
    entry.onMouseMove = [this, panel = m_panel](fw2::MouseMoveEvent& e) {
        return panel.contains(e.x, e.y);
    };
    entry.onScroll = [this, panel = m_panel](fw2::ScrollEvent& e) {
        if (!panel.contains(e.x, e.y)) return false;
        m_scroll += e.dy * 24.0f;
        m_scroll = std::clamp(m_scroll, 0.0f, 100000.0f);
        return true;
    };
    entry.onKey = [this](fw2::KeyEvent& e) -> bool {
        const bool ctrl = (e.modifiers & fw2::ModifierKey::Ctrl) != 0;
        const bool shift = (e.modifiers & fw2::ModifierKey::Shift) != 0;
        if (wantsKeys() && keyEvent(e.key, ctrl, shift)) return true;
        if (e.key == fw2::Key::Escape) { close(); return true; }
        return false;
    };
    entry.onEscape = [this]() { close(); };
    m_handle = ctx.layerStack->push(OverlayLayer::Overlay, std::move(entry));
}

bool LiveCodeConsole::handleMouseDown(fw2::MouseEvent& e, const Rect& panel) {
    if (!panel.contains(e.x, e.y)) return false;
    if (!m_mgr) return true;
    const float lx = e.x - panel.x, ly = e.y - panel.y;
    switch (zoneAt(panel, lx, ly)) {
        case Zone::RunStop:
            if (m_tab == 2) {
                // Edit tab: Run evaluates the buffer directly.
                if (m_editor.onEvaluate) m_editor.onEvaluate(m_editor.kernel().text());
            } else if (m_mgr->isActive()) {
                m_mgr->stop();
            } else {
                m_mgr->runScript();
            }
            break;
        case Zone::Reload:
            m_mgr->reload();
            break;
        case Zone::Clear:
            m_mgr->clearConsole();
            break;
        case Zone::TabLog:
            m_tab = 0;
            m_scroll = 0.0f;
            break;
        case Zone::TabCode:
            m_tab = 1;
            m_scroll = 0.0f;
            refreshCodeLens();
            break;
        case Zone::TabEdit:
            m_tab = 2;
            m_scroll = 0.0f;
            loadEditorBuffer();
            break;
        case Zone::Save:
            if (m_tab == 2) saveBuffer();
            else if (m_tab == 1) syncScript();
            break;
        case Zone::None:
            if (m_tab == 2 && m_lastLineH > 0.0f) {
                // Click inside the editor area → caret placement.
                const float areaY = kHeaderH + 2.0f + kTabH + 4.0f;
                const float areaH = panel.h - kPad - areaY;
                const float ly2 = ly - areaY;
                if (ly2 >= 0.0f && ly2 < areaH) {
                    const Rect content{0.0f, areaY, panel.w, areaH};
                    m_editor.click(lx, ly2, m_lastLineH,
                                   fw2::theme().metrics.fontSizeSmall,
                                   *m_lastMet, content);
                }
            }
            break;
    }
    return true;
}

void LiveCodeConsole::tick() {
    if (!isOpen() || !m_mgr || !m_project || !m_engine) return;
    if (m_tab != 1) return;
    m_codeLensAge += 1.0 / 60.0;
    if (m_codeLensAge >= 1.0 || m_codeLensDirty) refreshCodeLens();
}

void LiveCodeConsole::refreshCodeLens() {
    if (m_mgr && m_project && m_engine)
        m_codeLens = livecode::generateSongSource(*m_project, *m_engine);
    m_codeLensAge = 0.0;
    m_codeLensDirty = false;
}

// ─── Edit tab ────────────────────────────────────────────────────────

void LiveCodeConsole::loadEditorBuffer() {
    if (!m_mgr) return;
    // Fresh from disk when the buffer has no unsaved edits; otherwise
    // keep what the user typed.
    if (m_editorLoaded && m_editor.kernel().modified()) return;
    if (!m_editorLoaded || !m_editor.kernel().modified()) {
        std::ifstream f(m_mgr->defaultScriptPath(), std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            m_editor.kernel().setText(ss.str());
        } else {
            m_editor.kernel().setText("-- press Run to create the template\n");
        }
        m_editor.kernel().clearModified();
        m_editorLoaded = true;
    }
}

bool LiveCodeConsole::keyEvent(fw2::Key key, bool ctrl, bool shift) {
    if (m_tab != 2) return false;
    return m_editor.keyDown(key, ctrl, shift);
}

void LiveCodeConsole::forwardTextInput(const std::string& t) {
    if (m_tab == 2) m_editor.textInput(t);
}

void LiveCodeConsole::saveBuffer() {
    if (!m_mgr || m_tab != 2) return;
    const std::string path = m_mgr->defaultScriptPath();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        m_mgr->showToast("Live code: cannot save " + path, 2.5f, 2);
        return;
    }
    f << m_editor.kernel().text();
    f.close();
    m_editor.kernel().clearModified();
    m_mgr->pushConsole(0, "live code: saved " + path);
    m_mgr->showToast("Live code script saved", 1.5f, 0);
}

void LiveCodeConsole::syncScript() {
    if (!m_mgr || !m_project || !m_engine) return;
    const std::string path = m_mgr->defaultScriptPath();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        m_mgr->pushConsole(1, "live code: no script file to sync");
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const auto rep = livecode::patchSongSource(ss.str(), *m_project, *m_engine);
    for (const auto& w : rep.warnings) m_mgr->pushConsole(1, "sync: " + w);
    if (!rep.changed) {
        m_mgr->pushConsole(0, "live code: script already in sync");
        return;
    }
    std::ofstream g(path, std::ios::binary | std::ios::trunc);
    if (!g) {
        m_mgr->pushConsole(2, "live code: cannot write " + path);
        return;
    }
    g << rep.patched;
    g.close();
    m_editorLoaded = false;   // Edit tab reloads the patched file next time
    m_codeLensDirty = true;
    m_mgr->pushConsole(0, "live code: script patched from project state");
    m_mgr->showToast("Script synced from project", 1.5f, 0);
}

void LiveCodeConsole::paintBody(fw2::UIContext& ctx, const Rect& panel) {
    Renderer2D& r = *ctx.renderer;
    if (!ctx.textMetrics || !m_mgr) return;
    const auto& pal = theme().palette;
    const auto& met = theme().metrics;
    const float lineH = ctx.textMetrics->lineHeight(met.fontSizeSmall);
    m_lastLineH = lineH;
    m_lastMet = ctx.textMetrics;

    // Panel background + border.
    r.drawRect(panel.x, panel.y, panel.w, panel.h, pal.panelBg.withAlpha(238));
    r.drawRectOutline(panel.x, panel.y, panel.w, panel.h, pal.border);

    // ── Header: status text (left) + buttons (right) ──
    {
        std::string status = "live code";
        if (m_mgr->isActive())
            status += " — gen " + std::to_string(m_mgr->activeGeneration()) + " running";
        else
            status += " — stopped";
        status += " · " + std::to_string(m_mgr->pendingSchedules()) + " sched";
        const int pr = m_mgr->prerenderActive();
        if (pr > 0) status += " · " + std::to_string(pr) + " prerender";
        ctx.textMetrics->drawText(r, status, panel.x + kPad, panel.y + 8.0f,
                                  met.fontSizeSmall, pal.textPrimary);
    }
    const auto drawBtn = [&](Zone zone, const std::string& label) {
        float x = 0.0f;
        switch (zone) {
            case Zone::Clear:  x = panel.w - kPad - kBtnW; break;
            case Zone::Reload: x = panel.w - kPad - 2.0f * (kBtnW + 6.0f); break;
            case Zone::RunStop:
                x = panel.w - kPad - 3.0f * (kBtnW + 6.0f); break;
            case Zone::Save:
                x = panel.w - kPad - 4.0f * (kBtnW + 6.0f); break;
            default:           x = panel.w - kPad - 3.0f * (kBtnW + 6.0f); break;
        }
        const Rect br{panel.x + x, panel.y + 4.0f, kBtnW, kBtnH};
        r.drawRect(br.x, br.y, br.w, br.h, pal.surface);
        r.drawRectOutline(br.x, br.y, br.w, br.h, pal.borderSubtle);
        const float tw = ctx.textMetrics->textWidth(label, met.fontSizeSmall);
        ctx.textMetrics->drawText(r, label, br.x + (br.w - tw) * 0.5f,
                                  br.y + 4.0f, met.fontSizeSmall,
                                  pal.textPrimary);
        return br;
    };
    drawBtn(Zone::RunStop, m_mgr->isActive() ? "Stop" : "Run");
    drawBtn(Zone::Reload,  "Reload");
    drawBtn(Zone::Clear,   "Clear");
    if (m_tab == 2) drawBtn(Zone::Save, "Save");
    else if (m_tab == 1) drawBtn(Zone::Save, "Sync");

    // ── Tab row ──
    {
        const float tabY = panel.y + kHeaderH + 2.0f;
        const auto drawTab = [&](int idx, bool active, const std::string& label) {
            const Rect tr{panel.x + kPad + idx * (kTabW + 6.0f), tabY, kTabW, kTabH};
            r.drawRect(tr.x, tr.y, tr.w, tr.h,
                active ? pal.elevated : pal.surface);
            if (active)
                r.drawRect(tr.x, tr.y + tr.h - 2.0f, tr.w, 2.0f, pal.accent);
            const float tw = ctx.textMetrics->textWidth(label, met.fontSizeSmall);
            ctx.textMetrics->drawText(r, label, tr.x + (tr.w - tw) * 0.5f,
                                      tr.y + 5.0f, met.fontSizeSmall,
                                      active ? pal.textPrimary : pal.textSecondary);
        };
        drawTab(0, m_tab == 0, "Console");
        drawTab(1, m_tab == 1, "Code");
        drawTab(2, m_tab == 2, "Edit");
    }

    // ── Content area ──
    const float areaY = panel.y + kHeaderH + 2.0f + kTabH + 4.0f;
    const float areaH = panel.y + panel.h - kPad - areaY;
    if (areaH <= 0) return;

    r.pushClip(panel.x, areaY, panel.w, areaH);

    if (m_tab == 0) {
        // Console log — newest at the bottom, wheel scrolls into history.
        const auto& lines = m_mgr->console();
        const size_t total = lines.size();
        const size_t visible = std::max<size_t>(
            1, static_cast<size_t>(areaH / lineH));
        size_t back = static_cast<size_t>(m_scroll / lineH);
        back = std::min(back, total > visible ? total - visible : 0);
        const size_t first = total > visible + back ? total - visible - back : 0;
        float y = areaY + (areaH - static_cast<float>(visible) * lineH) * 0.5f;
        for (size_t i = first; i < total && y < areaY + areaH; ++i, y += lineH) {
            const auto& ln = lines[i];
            const auto col = (ln.severity == 2) ? pal.error
                           : (ln.severity == 1) ? pal.warn
                                                  : pal.textPrimary;
            ctx.textMetrics->drawText(r, ln.text, panel.x + kPad, y,
                                      met.fontSizeSmall, col);
        }
    } else if (m_tab == 2) {
        // Editor.
        m_editor.paint(ctx, Rect{panel.x, areaY, panel.w, areaH});
    } else {
        // Code lens — regenerated project description.
        const auto lines = splitLines(m_codeLens);
        const size_t total = lines.size();
        const size_t visible = std::max<size_t>(
            1, static_cast<size_t>(areaH / lineH));
        size_t back = static_cast<size_t>(m_scroll / lineH);
        back = std::min(back, total > visible ? total - visible : 0);
        const size_t first = total > visible + back ? total - visible - back : 0;
        float y = areaY;
        for (size_t i = 0; i < total; ++i, y += lineH) {
            if (i < first) continue;
            if (y >= areaY + areaH) break;
            const auto col = (i < 3) ? pal.textSecondary : pal.textPrimary;
            ctx.textMetrics->drawText(r, lines[i], panel.x + kPad, y,
                                      met.fontSizeSmall, col);
        }
    }

    r.popClip();
}

} // namespace ui
} // namespace yawn
