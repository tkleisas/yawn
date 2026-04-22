#pragma once

// UI v2 — FwExportDialog.
//
// Modal audio-export dialog. Two visual modes:
//   • Config mode — FwDropDown widgets for format / bit-depth /
//     sample-rate / scope, plus a summary line. OK fires the caller's
//     onResult(OK) so the hosting app can pop a native save dialog
//     and kick off the render.
//   • Render mode — FwProgressBar + "Press Escape or Cancel to abort"
//     hint. Dropdowns hidden; OK button disabled; Cancel becomes
//     "Cancel Render" and signals abort via progress.cancelled.
//
// Replaces v1 fw::ExportDialog. Public API mirrors the original so
// App wiring barely changes.

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/ProgressBar.h"

#include "audio/OfflineRenderer.h"
#include "util/FileIO.h"

#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

enum class ExportResult { OK, Cancel };

class FwExportDialog {
public:
    struct Config {
        util::ExportFormat  format     = util::ExportFormat::WAV;
        util::BitDepth      bitDepth   = util::BitDepth::Float32;
        int                 sampleRate = 44100;
        int                 scope      = 0;   // 0 = full arrangement, 1 = loop region
        double              arrangementLengthBeats = 64.0;
        double              loopStartBeats = 0.0;
        double              loopEndBeats   = 0.0;
        bool                loopEnabled    = false;
    };

    using ResultCallback = std::function<void(ExportResult)>;

    FwExportDialog();
    ~FwExportDialog();

    FwExportDialog(const FwExportDialog&)            = delete;
    FwExportDialog& operator=(const FwExportDialog&) = delete;

    // ─── Public API ──────────────────────────────────────────────
    void open(const Config& config);
    void close(ExportResult r = ExportResult::Cancel);
    bool isOpen() const { return m_handle.active(); }

    const Config& config() const { return m_config; }

    // Render-mode control.
    audio::RenderProgress& progress()       { return m_progress; }
    bool  isRendering() const               { return m_rendering; }
    void  setRendering(bool r);
    void  forceClose();   // called when render completes / is cancelled

    void setOnResult(ResultCallback cb) { m_onResult = std::move(cb); }

    // Paint hook — invoked by the overlay-entry paint closure.
    void paintBody(UIContext& ctx);

private:
    // Chrome metrics.
    static constexpr float kTitleBarHeight  = 32.0f;
    static constexpr float kFooterHeight    = 40.0f;
    static constexpr float kPadding         = 8.0f;
    static constexpr float kCloseButtonSize = 24.0f;

    // ─── Setup ─────────────────────────────────────────────────────
    void configureDropdowns();
    void syncDropdownsToConfig();

    // ─── Geometry ──────────────────────────────────────────────────
    Rect bodyRect(const UIContext& ctx) const;
    Rect closeButtonRect(const Rect& body) const;
    Rect okButtonRect(const Rect& body) const;
    Rect cancelButtonRect(const Rect& body) const;

    // ─── Paint ─────────────────────────────────────────────────────
    void paintConfigMode(UIContext& ctx, Rect content);
    void paintRenderMode(UIContext& ctx, Rect content);
    void drawLabeledRow(UIContext& ctx, const char* label,
                        Rect labelArea, float textScale);

    // ─── Event dispatch ────────────────────────────────────────────
    Widget* findWidgetAt(float sx, float sy);
    void    forwardMouse(Widget* w, MouseEvent& e,
                         bool (Widget::*fn)(MouseEvent&));
    void    forwardMouseMove(Widget* w, MouseMoveEvent& e);

    // ─── Overlay callbacks ─────────────────────────────────────────
    bool onMouseDown(MouseEvent& e);
    bool onMouseUp(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onDismiss();

    // ─── State ──────────────────────────────────────────────────────
    Config                 m_config;
    audio::RenderProgress  m_progress;
    bool                   m_rendering = false;

    float m_preferredW = 420.0f;
    float m_preferredH = 340.0f;
    Rect  m_body{};

    // Widgets.
    FwDropDown      m_formatDD;
    FwDropDown      m_bitDepthDD;
    FwDropDown      m_sampleRateDD;
    FwDropDown      m_scopeDD;
    FwProgressBar   m_progressBar;

    ExportResult    m_result  = ExportResult::Cancel;
    ResultCallback  m_onResult;
    OverlayHandle   m_handle;
    bool            m_closing = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
