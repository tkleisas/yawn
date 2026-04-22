// FwExportDialog.cpp — main-exe-only implementation.

#include "ExportDialog.h"

#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/LayerStack.h"

#include "ui/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Lifetime
// ───────────────────────────────────────────────────────────────────

FwExportDialog::FwExportDialog() {
    configureDropdowns();
    m_progressBar.setMinLength(180.0f);
}

FwExportDialog::~FwExportDialog() {
    if (m_handle.active()) {
        m_closing = true;
        m_handle.remove();
    }
}

// ───────────────────────────────────────────────────────────────────
// Public API
// ───────────────────────────────────────────────────────────────────

void FwExportDialog::open(const Config& config) {
    // Re-open is legal — tear down the existing overlay silently.
    if (m_handle.active()) {
        m_closing = true;
        m_handle.remove();
        m_closing = false;
    }

    m_config = config;
    // Default to loop region if loop is enabled + non-empty.
    if (config.loopEnabled && config.loopEndBeats > config.loopStartBeats) {
        m_config.scope = 1;
    } else {
        m_config.scope = 0;
    }
    m_rendering = false;
    m_progress.fraction.store(0.0f);
    m_progress.done.store(false);
    m_progress.cancelled.store(false);
    m_progress.failed.store(false);
    m_result = ExportResult::Cancel;

    syncDropdownsToConfig();

    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;

    OverlayEntry entry;
    entry.debugName             = "ExportDialog";
    entry.bounds                = ctx.viewport;
    entry.modal                 = true;
    entry.dismissOnOutsideClick = false;
    entry.paint       = [this](UIContext& c)      { this->paintBody(c); };
    entry.onMouseDown = [this](MouseEvent& e)     { return this->onMouseDown(e); };
    entry.onMouseUp   = [this](MouseEvent& e)     { return this->onMouseUp(e);   };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return this->onMouseMove(e); };
    entry.onKey       = [this](KeyEvent& e)       { return this->onKey(e); };
    entry.onDismiss   = [this]()                  { this->onDismiss(); };

    m_handle = ctx.layerStack->push(OverlayLayer::Modal, std::move(entry));
}

void FwExportDialog::close(ExportResult r) {
    if (!m_handle.active()) return;
    if (m_rendering) {
        // Cancel-close during render: flag cancel but keep overlay
        // open — caller polls progress.done and calls forceClose()
        // when the render thread finishes.
        m_progress.cancelled.store(true);
        return;
    }
    m_result  = r;
    m_closing = false;
    m_handle.remove();
}

void FwExportDialog::setRendering(bool r) {
    m_rendering = r;
    // Drive the progress bar's indeterminate state. In render mode we
    // use determinate (setValue per poll from progress.fraction); when
    // finished we let forceClose() tear the overlay down.
    if (r) m_progressBar.setDeterminate();
}

void FwExportDialog::forceClose() {
    m_rendering = false;
    if (!m_handle.active()) return;
    m_closing = true;   // suppress onResult fire — render path manages its own notifications
    m_handle.remove();
}

void FwExportDialog::onDismiss() {
    m_handle.detach_noRemove();
    if (m_closing) { m_closing = false; return; }
    if (m_onResult) m_onResult(m_result);
}

// ───────────────────────────────────────────────────────────────────
// Dropdown setup
// ───────────────────────────────────────────────────────────────────

void FwExportDialog::configureDropdowns() {
    using Labels = std::vector<std::string>;

    m_formatDD.setItems(Labels{"WAV", "FLAC", "OGG"});
    m_formatDD.setOnChange([this](int idx, const std::string&) {
        if (idx < 0 || idx > 2) return;
        m_config.format = static_cast<util::ExportFormat>(idx);
        // OGG has no bit-depth choice (always lossy) — clamp to float.
        if (m_config.format == util::ExportFormat::OGG) {
            m_config.bitDepth = util::BitDepth::Float32;
        }
    });

    m_bitDepthDD.setItems(Labels{"16-bit", "24-bit", "32-bit float"});
    m_bitDepthDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx <= 2)
            m_config.bitDepth = static_cast<util::BitDepth>(idx);
    });

    m_sampleRateDD.setItems(Labels{"44100 Hz", "48000 Hz", "96000 Hz"});
    m_sampleRateDD.setOnChange([this](int idx, const std::string&) {
        static const int rates[] = {44100, 48000, 96000};
        if (idx >= 0 && idx < 3) m_config.sampleRate = rates[idx];
    });

    m_scopeDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < 2) m_config.scope = idx;
    });
}

void FwExportDialog::syncDropdownsToConfig() {
    m_formatDD.setSelectedIndex(static_cast<int>(m_config.format),
                                 ValueChangeSource::Programmatic);

    const bool oggDisabled = (m_config.format == util::ExportFormat::OGG);
    m_bitDepthDD.setEnabled(!oggDisabled);
    m_bitDepthDD.setSelectedIndex(static_cast<int>(m_config.bitDepth),
                                   ValueChangeSource::Programmatic);

    int srIdx = 0;
    if (m_config.sampleRate == 48000) srIdx = 1;
    else if (m_config.sampleRate == 96000) srIdx = 2;
    m_sampleRateDD.setSelectedIndex(srIdx, ValueChangeSource::Programmatic);

    const bool loopScopeAvailable =
        m_config.loopEnabled && m_config.loopEndBeats > m_config.loopStartBeats;
    using Labels = std::vector<std::string>;
    if (loopScopeAvailable) {
        m_scopeDD.setItems(Labels{"Full Arrangement", "Loop Region"});
    } else {
        m_scopeDD.setItems(Labels{"Full Arrangement"});
    }
    const int scope = std::min(m_config.scope, m_scopeDD.itemCount() - 1);
    m_scopeDD.setSelectedIndex(std::max(0, scope), ValueChangeSource::Programmatic);
}

// ───────────────────────────────────────────────────────────────────
// Geometry
// ───────────────────────────────────────────────────────────────────

Rect FwExportDialog::bodyRect(const UIContext& ctx) const {
    const Rect& v = ctx.viewport;
    const float dx = v.x + (v.w - m_preferredW) * 0.5f;
    const float dy = v.y + (v.h - m_preferredH) * 0.5f;
    return Rect{dx, dy, m_preferredW, m_preferredH};
}

Rect FwExportDialog::closeButtonRect(const Rect& body) const {
    return Rect{body.x + body.w - kCloseButtonSize - 4.0f,
                body.y + (kTitleBarHeight - kCloseButtonSize) * 0.5f,
                kCloseButtonSize, kCloseButtonSize};
}

Rect FwExportDialog::okButtonRect(const Rect& body) const {
    constexpr float bw = 90.0f, bh = 32.0f;
    return Rect{body.x + body.w - kPadding - bw,
                body.y + body.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
}

Rect FwExportDialog::cancelButtonRect(const Rect& body) const {
    constexpr float bw = 120.0f, bh = 32.0f;   // wider for "Cancel Render"
    return Rect{body.x + body.w - kPadding - 90.0f - kPadding - bw,
                body.y + body.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
}

void FwExportDialog::drawLabeledRow(UIContext& ctx, const char* label,
                                      Rect labelArea, float textScale) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& tm = *ctx.textMetrics;
    const float textH = tm.lineHeight(textScale);
    tm.drawText(*ctx.renderer, label, labelArea.x,
                labelArea.y + (labelArea.h - textH) * 0.5f,
                textScale, ::yawn::ui::Theme::textSecondary);
}

// ───────────────────────────────────────────────────────────────────
// Paint
// ───────────────────────────────────────────────────────────────────

void FwExportDialog::paintBody(UIContext& ctx) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    m_body = bodyRect(ctx);
    const float dx = m_body.x, dy = m_body.y;
    const float dw = m_body.w, dh = m_body.h;

    // Body + border.
    r.drawRect(dx, dy, dw, dh, Color{45, 45, 52, 255});
    r.drawRectOutline(dx, dy, dw, dh, Color{75, 75, 85, 255});

    const ThemeMetrics& m = theme().metrics;
    const float titleSize = m.fontSizeLarge;
    const float btnSize   = m.fontSize;

    // Title bar.
    r.drawRect(dx, dy, dw, kTitleBarHeight, Color{55, 55, 62, 255});
    tm.drawText(r, "Export Audio", dx + 12,
                dy + (kTitleBarHeight - tm.lineHeight(titleSize)) * 0.5f,
                titleSize, ::yawn::ui::Theme::textPrimary);

    // Close button.
    const Rect cb = closeButtonRect(m_body);
    r.drawRect(cb.x, cb.y, cb.w, cb.h, Color{160, 50, 50, 255});
    const float xSize = m.fontSizeSmall;
    tm.drawText(r, "X",
                cb.x + (cb.w - tm.textWidth("X", xSize)) * 0.5f,
                cb.y + (cb.h - tm.lineHeight(xSize)) * 0.5f,
                xSize, ::yawn::ui::Theme::textPrimary);

    // Content area.
    const Rect content{dx + 16.0f,
                        dy + kTitleBarHeight + 12.0f,
                        dw - 32.0f,
                        dh - kTitleBarHeight - kFooterHeight - 24.0f};

    if (m_rendering) paintRenderMode(ctx, content);
    else             paintConfigMode(ctx, content);

    // Footer.
    const float footerY = dy + dh - kFooterHeight;
    r.drawRect(dx, footerY, dw, kFooterHeight, Color{50, 50, 55, 255});

    // OK button (disabled during render).
    const Rect okR = okButtonRect(m_body);
    const Color okBg = m_rendering ? Color{50, 50, 55, 255}
                                   : Color{50, 130, 50, 255};
    r.drawRect(okR.x, okR.y, okR.w, okR.h, okBg);
    tm.drawText(r, "Export",
                okR.x + (okR.w - tm.textWidth("Export", btnSize)) * 0.5f,
                okR.y + (okR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);

    // Cancel / Cancel Render button.
    const Rect cancelR = cancelButtonRect(m_body);
    r.drawRect(cancelR.x, cancelR.y, cancelR.w, cancelR.h,
                Color{130, 50, 50, 255});
    const char* cancelTxt = m_rendering ? "Cancel Render" : "Cancel";
    tm.drawText(r, cancelTxt,
                cancelR.x + (cancelR.w - tm.textWidth(cancelTxt, btnSize)) * 0.5f,
                cancelR.y + (cancelR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);
}

void FwExportDialog::paintConfigMode(UIContext& ctx, Rect content) {
    const ThemeMetrics& m = theme().metrics;
    const float textScale = m.fontSizeSmall;
    const float ctrlH     = m.controlHeight;
    const float rowH      = std::max(34.0f, ctrlH + 4.0f);
    const float rowGap    = 6.0f;
    const float dropW     = std::min(content.w * 0.55f, 220.0f);
    const float dropX     = content.x + content.w - dropW;

    float y = content.y;

    // Format
    drawLabeledRow(ctx, "Format",
                   Rect{content.x, y, dropX - content.x, rowH}, textScale);
    m_formatDD.measure(Constraints::tight(dropW, ctrlH), ctx);
    m_formatDD.layout(Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH}, ctx);
    m_formatDD.render(ctx);
    y += rowH + rowGap;

    // Bit Depth (disabled for OGG)
    drawLabeledRow(ctx, "Bit Depth",
                   Rect{content.x, y, dropX - content.x, rowH}, textScale);
    const bool oggDisabled = (m_config.format == util::ExportFormat::OGG);
    m_bitDepthDD.setEnabled(!oggDisabled);
    m_bitDepthDD.measure(Constraints::tight(dropW, ctrlH), ctx);
    m_bitDepthDD.layout(Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH}, ctx);
    if (oggDisabled) {
        // Swap in a read-only label look.
        auto& r  = *ctx.renderer;
        auto& tm = *ctx.textMetrics;
        r.drawRect(dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH,
                    Color{35, 35, 38, 255});
        r.drawRectOutline(dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH,
                           Color{55, 55, 60, 255});
        const float textH = tm.lineHeight(textScale);
        tm.drawText(r, "N/A (lossy)", dropX + 8.0f,
                    y + (rowH - ctrlH) * 0.5f + (ctrlH - textH) * 0.5f,
                    textScale, ::yawn::ui::Theme::textSecondary);
    } else {
        m_bitDepthDD.render(ctx);
    }
    y += rowH + rowGap;

    // Sample Rate
    drawLabeledRow(ctx, "Sample Rate",
                   Rect{content.x, y, dropX - content.x, rowH}, textScale);
    m_sampleRateDD.measure(Constraints::tight(dropW, ctrlH), ctx);
    m_sampleRateDD.layout(Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH}, ctx);
    m_sampleRateDD.render(ctx);
    y += rowH + rowGap;

    // Scope
    drawLabeledRow(ctx, "Scope",
                   Rect{content.x, y, dropX - content.x, rowH}, textScale);
    m_scopeDD.measure(Constraints::tight(dropW, ctrlH), ctx);
    m_scopeDD.layout(Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH}, ctx);
    m_scopeDD.render(ctx);
    y += rowH + rowGap;

    // Summary.
    y += 8.0f;
    double startBeat = 0.0, endBeat = m_config.arrangementLengthBeats;
    if (m_config.scope == 1) {
        startBeat = m_config.loopStartBeats;
        endBeat   = m_config.loopEndBeats;
    }
    const double beats = endBeat - startBeat;
    char summary[128];
    std::snprintf(summary, sizeof(summary),
                  "%.0f beats (%.1f bars)", beats, beats / 4.0);
    drawLabeledRow(ctx, summary, Rect{content.x, y, content.w, 20.0f},
                   textScale);
}

void FwExportDialog::paintRenderMode(UIContext& ctx, Rect content) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const ThemeMetrics& m = theme().metrics;
    const float textScale = m.fontSize;

    // Sync progress bar value from render thread.
    const float pct = m_progress.fraction.load();
    m_progressBar.setValue(pct, ValueChangeSource::Programmatic);
    m_progressBar.setError(m_progress.failed.load());

    // Status text centered above the bar.
    char status[64];
    std::snprintf(status, sizeof(status), "Rendering... %.0f%%", pct * 100.0f);
    const float sw = tm.textWidth(status, textScale);
    const float lh = tm.lineHeight(textScale);
    tm.drawText(r, status,
                content.x + (content.w - sw) * 0.5f,
                content.y + 30.0f,
                textScale, ::yawn::ui::Theme::textPrimary);

    // Progress bar.
    const float barH = 14.0f;
    const Rect barRect{content.x, content.y + 30.0f + lh + 12.0f,
                        content.w, barH};
    m_progressBar.measure(Constraints::tight(barRect.w, barRect.h), ctx);
    m_progressBar.layout(barRect, ctx);
    m_progressBar.render(ctx);

    // Hint below.
    const char* hint = "Press Escape or Cancel to abort";
    const float hw = tm.textWidth(hint, textScale);
    tm.drawText(r, hint,
                content.x + (content.w - hw) * 0.5f,
                barRect.y + barH + 12.0f,
                textScale, ::yawn::ui::Theme::textSecondary);
}

// ───────────────────────────────────────────────────────────────────
// Event dispatch
// ───────────────────────────────────────────────────────────────────

Widget* FwExportDialog::findWidgetAt(float sx, float sy) {
    if (m_rendering) return nullptr;
    Widget* candidates[] = {&m_formatDD, &m_bitDepthDD, &m_sampleRateDD, &m_scopeDD};
    for (Widget* w : candidates) {
        if (!w || !w->isVisible() || !w->isEnabled()) continue;
        if (w->hitTestGlobal(sx, sy)) return w;
    }
    return nullptr;
}

void FwExportDialog::forwardMouse(Widget* w, MouseEvent& e,
                                    bool (Widget::*fn)(MouseEvent&)) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    (w->*fn)(e);
}

void FwExportDialog::forwardMouseMove(Widget* w, MouseMoveEvent& e) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    w->dispatchMouseMove(e);
}

bool FwExportDialog::onMouseDown(MouseEvent& e) {
    const float mx = e.x, my = e.y;

    // Outside body → cancel (but only if not rendering; during render
    // the user must explicitly click Cancel Render).
    if (mx < m_body.x || mx > m_body.x + m_body.w ||
        my < m_body.y || my > m_body.y + m_body.h) {
        if (!m_rendering) close(ExportResult::Cancel);
        return true;
    }

    // Close button.
    const Rect cb = closeButtonRect(m_body);
    if (mx >= cb.x && mx <= cb.x + cb.w && my >= cb.y && my <= cb.y + cb.h) {
        close(ExportResult::Cancel);
        return true;
    }

    // OK button (disabled during render).
    if (!m_rendering) {
        const Rect okR = okButtonRect(m_body);
        if (mx >= okR.x && mx <= okR.x + okR.w && my >= okR.y && my <= okR.y + okR.h) {
            close(ExportResult::OK);
            return true;
        }
    }

    // Cancel / Cancel Render button.
    const Rect cancelR = cancelButtonRect(m_body);
    if (mx >= cancelR.x && mx <= cancelR.x + cancelR.w &&
        my >= cancelR.y && my <= cancelR.y + cancelR.h) {
        close(ExportResult::Cancel);
        return true;
    }

    // Content-area widget dispatch (dropdowns in config mode only).
    Widget* w = findWidgetAt(mx, my);
    if (w) forwardMouse(w, e, &Widget::dispatchMouseDown);
    return true;
}

bool FwExportDialog::onMouseUp(MouseEvent& e) {
    Widget* cap = Widget::capturedWidget();
    if (cap) forwardMouse(cap, e, &Widget::dispatchMouseUp);
    return true;
}

bool FwExportDialog::onMouseMove(MouseMoveEvent& e) {
    Widget* cap = Widget::capturedWidget();
    if (cap) { forwardMouseMove(cap, e); return true; }
    if (m_rendering) return true;
    Widget* widgets[] = {&m_formatDD, &m_bitDepthDD, &m_sampleRateDD, &m_scopeDD};
    for (Widget* w : widgets) {
        if (!w) continue;
        MouseMoveEvent copy = e;
        forwardMouseMove(w, copy);
    }
    return true;
}

bool FwExportDialog::onKey(KeyEvent& e) {
    if (e.consumed) return false;
    if (e.key == Key::Escape) { close(ExportResult::Cancel); return true; }
    // Enter commits (Export) only outside render mode.
    if (!m_rendering && e.key == Key::Enter) {
        close(ExportResult::OK);
        return true;
    }
    return true;   // modal swallows
}

} // namespace fw2
} // namespace ui
} // namespace yawn
