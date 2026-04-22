// PreferencesDialog.cpp — UI v2 widget-driven implementation.
//
// Lives in the main executable build (not yawn_core) because the paint
// path touches v1 Renderer2D directly through UIContext::renderer —
// Renderer2D pulls glad/GL into its header.

#include "PreferencesDialog.h"

#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/LayerStack.h"

#include "ui/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

// ─── Theme-tab font-scale table ────────────────────────────────────
namespace {
constexpr float kFontScales[]    = {0.85f, 1.0f, 1.25f, 1.5f, 1.75f};
constexpr int   kFontScaleCount  = 5;
std::vector<std::string> fontScaleLabels() {
    return {"Small (85%)", "Normal (100%)", "Large (125%)",
            "Extra Large (150%)", "XXL (175%)"};
}
int fontScaleIndex(float scale) {
    int best = 1;
    float bestErr = 1e9f;
    for (int i = 0; i < kFontScaleCount; ++i) {
        const float err = std::fabs(scale - kFontScales[i]);
        if (err < bestErr) { best = i; bestErr = err; }
    }
    return best;
}
} // anon

// ───────────────────────────────────────────────────────────────────
// Lifetime + configuration
// ───────────────────────────────────────────────────────────────────

FwPreferencesDialog::FwPreferencesDialog() {
    configureStaticDropdowns();

    // Build the tab strip. Tabs have null content — the dialog paints
    // each tab's widgets itself via its layoutAndRender* methods based
    // on m_tab. TabView just owns the strip's click routing, hover
    // tracking, and active-indicator paint.
    m_tabStrip.addTab("audio",     "Audio",     nullptr);
    m_tabStrip.addTab("midi",      "MIDI",      nullptr);
    m_tabStrip.addTab("defaults",  "Defaults",  nullptr);
    m_tabStrip.addTab("metronome", "Metronome", nullptr);
    m_tabStrip.addTab("theme",     "Theme",     nullptr);
    m_tabStrip.setOnActivated([this](const std::string& id) {
        // Keep the dialog's m_tab index in sync + close any open
        // dropdown popup so it doesn't dangle on a now-invisible
        // dropdown.
        m_tab = m_tabStrip.activeTabIndex();
        for (Widget* w : visibleWidgets()) {
            if (auto* dd = dynamic_cast<FwDropDown*>(w)) dd->close();
        }
    });
}

FwPreferencesDialog::~FwPreferencesDialog() {
    if (m_handle.active()) {
        m_closing = true;
        m_handle.remove();
    }
}

void FwPreferencesDialog::configureStaticDropdowns() {
    using Labels = std::vector<std::string>;

    // Sample rate
    m_sampleRateDD.setItems(Labels{"44100", "48000", "96000", "192000"});
    m_sampleRateDD.setOnChange([this](int idx, const std::string&) {
        static const double vals[] = {44100.0, 48000.0, 96000.0, 192000.0};
        if (idx >= 0 && idx < 4) m_state.sampleRate = vals[idx];
    });

    // Buffer size
    m_bufferSizeDD.setItems(Labels{"64", "128", "256", "512", "1024", "2048"});
    m_bufferSizeDD.setOnChange([this](int idx, const std::string&) {
        static const int vals[] = {64, 128, 256, 512, 1024, 2048};
        if (idx >= 0 && idx < 6) m_state.bufferSize = vals[idx];
    });

    // Defaults — launch / record quantize
    const Labels qmodes = {"None", "Beat", "Bar"};
    m_launchQDD.setItems(qmodes);
    m_launchQDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx <= 2)
            m_state.defaultLaunchQuantize = static_cast<audio::QuantizeMode>(idx);
    });
    m_recordQDD.setItems(qmodes);
    m_recordQDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx <= 2)
            m_state.defaultRecordQuantize = static_cast<audio::QuantizeMode>(idx);
    });

    // Metronome mode
    m_metroModeDD.setItems(Labels{"Always", "Record Only", "Playback Only", "Off"});
    m_metroModeDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < 4) m_state.metronomeMode = idx;
    });

    // Count-in bars
    m_countInDD.setItems(Labels{"None", "1 Bar", "2 Bars", "4 Bars"});
    m_countInDD.setOnChange([this](int idx, const std::string&) {
        static const int vals[] = {0, 1, 2, 4};
        if (idx >= 0 && idx < 4) m_state.countInBars = vals[idx];
    });

    // Metronome volume
    m_metroVolumeDD.setItems(Labels{"0% (visual only)", "10%", "20%", "30%", "40%",
                                      "50%", "60%", "70%", "80%", "90%", "100%"});
    m_metroVolumeDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx <= 10) m_state.metronomeVolume = idx * 0.1f;
    });

    // Visual style
    m_vizStyleDD.setItems(Labels{"Dots", "Beat Number"});
    m_vizStyleDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < 2) m_state.metronomeVisualStyle = idx;
    });

    // Font scale
    m_fontScaleDD.setItems(fontScaleLabels());
    m_fontScaleDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < kFontScaleCount)
            m_state.fontScale = kFontScales[idx];
    });

    // Device dropdowns — items populated per open(). Placeholder shown
    // when no device is selected.
    m_outputDD.setPlaceholder("Default Output");
    m_outputDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < static_cast<int>(m_outputDeviceIds.size()))
            m_state.selectedOutputDevice = m_outputDeviceIds[idx];
    });
    m_inputDD.setPlaceholder("Default Input");
    m_inputDD.setOnChange([this](int idx, const std::string&) {
        if (idx >= 0 && idx < static_cast<int>(m_inputDeviceIds.size()))
            m_state.selectedInputDevice = m_inputDeviceIds[idx];
    });
}

// ───────────────────────────────────────────────────────────────────
// Public API
// ───────────────────────────────────────────────────────────────────

void FwPreferencesDialog::open(State initialState,
                               audio::AudioEngine* engine,
                               midi::MidiEngine* midiEngine) {
    if (m_handle.active()) {
        m_closing = true;
        m_handle.remove();
        m_closing = false;
    }

    m_state      = std::move(initialState);
    m_engine     = engine;
    m_midiEngine = midiEngine;
    m_tab        = 0;
    m_result     = PreferencesResult::Cancel;
    // Re-open always lands on the Audio tab. Programmatic so we don't
    // fire the callback (which would redundantly close popups etc.).
    m_tabStrip.setActiveTab("audio", ValueChangeSource::Programmatic);

    refreshDevices();
    syncDropdownsToState();
    rebuildMidiChecks();

    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;

    OverlayEntry entry;
    entry.debugName             = "PreferencesDialog";
    entry.bounds                = ctx.viewport;   // cover everything
    entry.modal                 = true;
    entry.dismissOnOutsideClick = false;
    entry.paint       = [this](UIContext& c)       { this->paintBody(c); };
    entry.onMouseDown = [this](MouseEvent& e)      { return this->onMouseDown(e); };
    entry.onMouseUp   = [this](MouseEvent& e)      { return this->onMouseUp(e);   };
    entry.onMouseMove = [this](MouseMoveEvent& e)  { return this->onMouseMove(e); };
    entry.onKey       = [this](KeyEvent& e)        { return this->onKey(e); };
    entry.onDismiss   = [this]()                   { this->onDismiss(); };

    m_handle = ctx.layerStack->push(OverlayLayer::Modal, std::move(entry));
}

void FwPreferencesDialog::close(PreferencesResult r) {
    if (!m_handle.active()) return;
    m_result  = r;
    m_closing = false;
    m_handle.remove();
}

// ───────────────────────────────────────────────────────────────────
// Setup
// ───────────────────────────────────────────────────────────────────

void FwPreferencesDialog::refreshDevices() {
    m_audioDevices   = audio::AudioEngine::enumerateDevices();
    m_midiInputPorts  = midi::MidiPort::enumerateInputPorts();
    m_midiOutputPorts = midi::MidiPort::enumerateOutputPorts();
}

void FwPreferencesDialog::syncDropdownsToState() {
    // Build output / input device lists from m_audioDevices, filtered
    // by whether the device has usable channels.
    std::vector<std::string> outLabels;
    m_outputDeviceIds.clear();
    for (const auto& d : m_audioDevices) {
        if (d.maxOutputChannels > 0) {
            outLabels.push_back(d.name);
            m_outputDeviceIds.push_back(d.id);
        }
    }
    m_outputDD.setItems(outLabels);
    int outSel = -1;
    for (int i = 0; i < static_cast<int>(m_outputDeviceIds.size()); ++i) {
        if (m_outputDeviceIds[i] == m_state.selectedOutputDevice) { outSel = i; break; }
    }
    m_outputDD.setSelectedIndex(outSel, ValueChangeSource::Programmatic);

    std::vector<std::string> inLabels;
    m_inputDeviceIds.clear();
    for (const auto& d : m_audioDevices) {
        if (d.maxInputChannels > 0) {
            inLabels.push_back(d.name);
            m_inputDeviceIds.push_back(d.id);
        }
    }
    m_inputDD.setItems(inLabels);
    int inSel = -1;
    for (int i = 0; i < static_cast<int>(m_inputDeviceIds.size()); ++i) {
        if (m_inputDeviceIds[i] == m_state.selectedInputDevice) { inSel = i; break; }
    }
    m_inputDD.setSelectedIndex(inSel, ValueChangeSource::Programmatic);

    // Sample rate
    {
        static const double vals[] = {44100.0, 48000.0, 96000.0, 192000.0};
        int sel = 0;
        for (int i = 0; i < 4; ++i)
            if (std::abs(m_state.sampleRate - vals[i]) < 1.0) sel = i;
        m_sampleRateDD.setSelectedIndex(sel, ValueChangeSource::Programmatic);
    }

    // Buffer size
    {
        static const int vals[] = {64, 128, 256, 512, 1024, 2048};
        int sel = 2;
        for (int i = 0; i < 6; ++i)
            if (m_state.bufferSize == vals[i]) sel = i;
        m_bufferSizeDD.setSelectedIndex(sel, ValueChangeSource::Programmatic);
    }

    // Quantize modes
    {
        int l = static_cast<int>(m_state.defaultLaunchQuantize);
        if (l < 0 || l > 2) l = 2;
        m_launchQDD.setSelectedIndex(l, ValueChangeSource::Programmatic);
        int r = static_cast<int>(m_state.defaultRecordQuantize);
        if (r < 0 || r > 2) r = 2;
        m_recordQDD.setSelectedIndex(r, ValueChangeSource::Programmatic);
    }

    // Metronome
    m_metroModeDD.setSelectedIndex(std::clamp(m_state.metronomeMode, 0, 3),
                                    ValueChangeSource::Programmatic);
    {
        static const int vals[] = {0, 1, 2, 4};
        int sel = 0;
        for (int i = 0; i < 4; ++i)
            if (m_state.countInBars == vals[i]) sel = i;
        m_countInDD.setSelectedIndex(sel, ValueChangeSource::Programmatic);
    }
    m_metroVolumeDD.setSelectedIndex(
        std::clamp(static_cast<int>(m_state.metronomeVolume * 10.0f + 0.5f), 0, 10),
        ValueChangeSource::Programmatic);
    m_vizStyleDD.setSelectedIndex(std::clamp(m_state.metronomeVisualStyle, 0, 1),
                                   ValueChangeSource::Programmatic);

    // Font scale
    m_fontScaleDD.setSelectedIndex(fontScaleIndex(m_state.fontScale),
                                    ValueChangeSource::Programmatic);
}

void FwPreferencesDialog::rebuildMidiChecks() {
    m_midiInputChecks.clear();
    m_midiOutputChecks.clear();

    for (int i = 0; i < static_cast<int>(m_midiInputPorts.size()); ++i) {
        auto cb = std::make_unique<FwCheckbox>(m_midiInputPorts[i]);
        const bool enabled = std::find(m_state.enabledMidiInputs.begin(),
                                        m_state.enabledMidiInputs.end(), i)
                              != m_state.enabledMidiInputs.end();
        cb->setChecked(enabled, ValueChangeSource::Programmatic);
        const int portIdx = i;
        cb->setOnChange([this, portIdx](CheckState s) {
            auto& list = m_state.enabledMidiInputs;
            auto it = std::find(list.begin(), list.end(), portIdx);
            const bool on = (s == CheckState::On);
            if (on && it == list.end())      list.push_back(portIdx);
            else if (!on && it != list.end()) list.erase(it);
        });
        m_midiInputChecks.push_back(std::move(cb));
    }

    for (int i = 0; i < static_cast<int>(m_midiOutputPorts.size()); ++i) {
        auto cb = std::make_unique<FwCheckbox>(m_midiOutputPorts[i]);
        const bool enabled = std::find(m_state.enabledMidiOutputs.begin(),
                                        m_state.enabledMidiOutputs.end(), i)
                              != m_state.enabledMidiOutputs.end();
        cb->setChecked(enabled, ValueChangeSource::Programmatic);
        const int portIdx = i;
        cb->setOnChange([this, portIdx](CheckState s) {
            auto& list = m_state.enabledMidiOutputs;
            auto it = std::find(list.begin(), list.end(), portIdx);
            const bool on = (s == CheckState::On);
            if (on && it == list.end())      list.push_back(portIdx);
            else if (!on && it != list.end()) list.erase(it);
        });
        m_midiOutputChecks.push_back(std::move(cb));
    }
}

// ───────────────────────────────────────────────────────────────────
// Geometry
// ───────────────────────────────────────────────────────────────────

Rect FwPreferencesDialog::bodyRect(const UIContext& ctx) const {
    const Rect& v = ctx.viewport;
    const float dx = v.x + (v.w - m_preferredW) * 0.5f;
    const float dy = v.y + (v.h - m_preferredH) * 0.5f;
    return Rect{dx, dy, m_preferredW, m_preferredH};
}

Rect FwPreferencesDialog::closeButtonRect(const Rect& body) const {
    return Rect{body.x + body.w - kCloseButtonSize - 4.0f,
                body.y + (kTitleBarHeight - kCloseButtonSize) * 0.5f,
                kCloseButtonSize, kCloseButtonSize};
}

Rect FwPreferencesDialog::okButtonRect(const Rect& body) const {
    constexpr float bw = 80.0f, bh = 28.0f;
    return Rect{body.x + body.w - kPadding - bw,
                body.y + body.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
}

Rect FwPreferencesDialog::cancelButtonRect(const Rect& body) const {
    constexpr float bw = 80.0f, bh = 28.0f;
    return Rect{body.x + body.w - kPadding - bw - kPadding - bw,
                body.y + body.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
}

// ───────────────────────────────────────────────────────────────────
// Paint
// ───────────────────────────────────────────────────────────────────

void FwPreferencesDialog::paintBody(UIContext& ctx) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    m_body = bodyRect(ctx);
    const float dx = m_body.x, dy = m_body.y;
    const float dw = m_body.w, dh = m_body.h;

    // Body + border.
    r.drawRect(dx, dy, dw, dh, Color{45, 45, 52, 255});
    r.drawRectOutline(dx, dy, dw, dh, Color{75, 75, 85, 255});

    // Title bar.
    const ThemeMetrics& m = theme().metrics;
    const float titleSize = m.fontSizeLarge;
    r.drawRect(dx, dy, dw, kTitleBarHeight, Color{55, 55, 62, 255});
    tm.drawText(r, "Preferences", dx + 12,
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

    // Tab strip — lay out + render the TabView. Its bounds live right
    // below the title bar and above the content area; TabView paints
    // only the strip (content panes are nullptr, so nothing else
    // paints).
    const float stripH = m_tabStrip.tabStripHeight();
    const Rect  stripBounds{dx, dy + kTitleBarHeight, dw, stripH};
    m_tabStrip.measure(Constraints::tight(stripBounds.w, stripH), ctx);
    m_tabStrip.layout(stripBounds, ctx);
    m_tabStrip.render(ctx);

    // Content area, below the strip.
    const Rect content{dx + 16.0f,
                        dy + kTitleBarHeight + stripH + 8.0f,
                        dw - 32.0f,
                        dh - kTitleBarHeight - stripH - kFooterHeight - 16.0f};

    switch (m_tab) {
        case 0: layoutAndRenderAudioTab(ctx, content); break;
        case 1: layoutAndRenderMidiTab(ctx, content); break;
        case 2: layoutAndRenderDefaultsTab(ctx, content); break;
        case 3: layoutAndRenderMetronomeTab(ctx, content); break;
        default: layoutAndRenderThemeTab(ctx, content); break;
    }

    paintFooter(ctx);
}

void FwPreferencesDialog::paintFooter(UIContext& ctx) {
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const ThemeMetrics& m = theme().metrics;
    const float btnSize = m.fontSize;

    const float footerY = m_body.y + m_body.h - kFooterHeight;
    r.drawRect(m_body.x, footerY, m_body.w, kFooterHeight, Color{50, 50, 55, 255});

    const Rect okR = okButtonRect(m_body);
    r.drawRect(okR.x, okR.y, okR.w, okR.h, Color{50, 130, 50, 255});
    tm.drawText(r, "OK",
                okR.x + (okR.w - tm.textWidth("OK", btnSize)) * 0.5f,
                okR.y + (okR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);

    const Rect cancelR = cancelButtonRect(m_body);
    r.drawRect(cancelR.x, cancelR.y, cancelR.w, cancelR.h, Color{130, 50, 50, 255});
    tm.drawText(r, "Cancel",
                cancelR.x + (cancelR.w - tm.textWidth("Cancel", btnSize)) * 0.5f,
                cancelR.y + (cancelR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);
}

void FwPreferencesDialog::drawLabeledRow(UIContext& ctx, const char* label,
                                           Rect labelArea, float textScale) {
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const float textH = tm.lineHeight(textScale);
    tm.drawText(r, label, labelArea.x,
                labelArea.y + (labelArea.h - textH) * 0.5f,
                textScale, ::yawn::ui::Theme::textSecondary);
    (void)r;
}

// ───────────────────────────────────────────────────────────────────
// Per-tab layout + render
// ───────────────────────────────────────────────────────────────────

namespace {
// Lay out a single FwDropDown widget at (x, y, w, h), measure + render.
void placeAndRender(FwDropDown& w, UIContext& ctx, Rect bounds) {
    Constraints c = Constraints::tight(bounds.w, bounds.h);
    w.measure(c, ctx);
    w.layout(bounds, ctx);
    w.render(ctx);
}
void placeAndRender(FwCheckbox& w, UIContext& ctx, Rect bounds) {
    Constraints c = Constraints::tight(bounds.w, bounds.h);
    w.measure(c, ctx);
    w.layout(bounds, ctx);
    w.render(ctx);
}

// Standard row metrics — two columns (label on left, widget on right),
// 34 px row height, 6 px gap between rows.
constexpr float kRowH        = 34.0f;
constexpr float kRowGap      = 6.0f;
constexpr float kDropWFactor = 0.55f;   // dropdowns take 55% of content
constexpr float kDropWMax    = 260.0f;
} // anon

void FwPreferencesDialog::layoutAndRenderAudioTab(UIContext& ctx, Rect content) {
    const float ctrlH = theme().metrics.controlHeight;
    const float rowH  = std::max(kRowH, ctrlH + 4.0f);
    const float dropW = std::min(content.w * kDropWFactor, kDropWMax);
    const float dropX = content.x + content.w - dropW;
    const float textScale = theme().metrics.fontSizeSmall;

    float y = content.y;

    drawLabeledRow(ctx, "Audio Output Device", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_outputDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Audio Input Device", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_inputDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Sample Rate", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_sampleRateDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Buffer Size", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_bufferSizeDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
}

void FwPreferencesDialog::layoutAndRenderMidiTab(UIContext& ctx, Rect content) {
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const float textScale = theme().metrics.fontSizeSmall;
    const float textH     = tm.lineHeight(textScale);

    float y = content.y;

    tm.drawText(r, "MIDI Inputs (click to toggle)", content.x,
                y + (textH - textH) * 0.5f, textScale, ::yawn::ui::Theme::textSecondary);
    y += textH + 8.0f;

    for (auto& cb : m_midiInputChecks) {
        placeAndRender(*cb, ctx, Rect{content.x, y, content.w, 22.0f});
        y += 26.0f;
    }

    y += 10.0f;
    tm.drawText(r, "MIDI Outputs (click to toggle)", content.x,
                y, textScale, ::yawn::ui::Theme::textSecondary);
    y += textH + 8.0f;

    for (auto& cb : m_midiOutputChecks) {
        placeAndRender(*cb, ctx, Rect{content.x, y, content.w, 22.0f});
        y += 26.0f;
    }
}

void FwPreferencesDialog::layoutAndRenderDefaultsTab(UIContext& ctx, Rect content) {
    const float ctrlH = theme().metrics.controlHeight;
    const float rowH  = std::max(kRowH, ctrlH + 4.0f);
    const float dropW = std::min(content.w * kDropWFactor, kDropWMax);
    const float dropX = content.x + content.w - dropW;
    const float textScale = theme().metrics.fontSizeSmall;

    float y = content.y;
    drawLabeledRow(ctx, "Default Launch Quantize", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_launchQDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Default Record Quantize", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_recordQDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
}

void FwPreferencesDialog::layoutAndRenderMetronomeTab(UIContext& ctx, Rect content) {
    const float ctrlH = theme().metrics.controlHeight;
    const float rowH  = std::max(kRowH, ctrlH + 4.0f);
    const float dropW = std::min(content.w * kDropWFactor, kDropWMax);
    const float dropX = content.x + content.w - dropW;
    const float textScale = theme().metrics.fontSizeSmall;

    float y = content.y;

    drawLabeledRow(ctx, "Metronome Mode", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_metroModeDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Count-in Bars", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_countInDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Metronome Volume", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_metroVolumeDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
    y += rowH + kRowGap;

    drawLabeledRow(ctx, "Visual Style", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_vizStyleDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
}

void FwPreferencesDialog::layoutAndRenderThemeTab(UIContext& ctx, Rect content) {
    const float ctrlH = theme().metrics.controlHeight;
    const float rowH  = std::max(kRowH, ctrlH + 4.0f);
    const float dropW = std::min(content.w * kDropWFactor, kDropWMax);
    const float dropX = content.x + content.w - dropW;
    const float textScale = theme().metrics.fontSizeSmall;

    float y = content.y;
    drawLabeledRow(ctx, "UI Font Size", Rect{content.x, y, dropX - content.x, rowH}, textScale);
    placeAndRender(m_fontScaleDD, ctx, Rect{dropX, y + (rowH - ctrlH) * 0.5f, dropW, ctrlH});
}

// ───────────────────────────────────────────────────────────────────
// Event dispatch
// ───────────────────────────────────────────────────────────────────

std::vector<Widget*> FwPreferencesDialog::visibleWidgets() {
    std::vector<Widget*> out;
    switch (m_tab) {
        case 0:
            out.push_back(&m_outputDD);
            out.push_back(&m_inputDD);
            out.push_back(&m_sampleRateDD);
            out.push_back(&m_bufferSizeDD);
            break;
        case 1:
            for (auto& cb : m_midiInputChecks)  out.push_back(cb.get());
            for (auto& cb : m_midiOutputChecks) out.push_back(cb.get());
            break;
        case 2:
            out.push_back(&m_launchQDD);
            out.push_back(&m_recordQDD);
            break;
        case 3:
            out.push_back(&m_metroModeDD);
            out.push_back(&m_countInDD);
            out.push_back(&m_metroVolumeDD);
            out.push_back(&m_vizStyleDD);
            break;
        default:
            out.push_back(&m_fontScaleDD);
            break;
    }
    return out;
}

Widget* FwPreferencesDialog::findWidgetAt(float sx, float sy) {
    for (Widget* w : visibleWidgets()) {
        if (w && w->hitTestGlobal(sx, sy)) return w;
    }
    return nullptr;
}

void FwPreferencesDialog::forwardMouse(Widget* w, MouseEvent& e,
                                         bool (Widget::*fn)(MouseEvent&)) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    (w->*fn)(e);
}

void FwPreferencesDialog::forwardMouseMove(Widget* w, MouseMoveEvent& e) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    w->dispatchMouseMove(e);
}

bool FwPreferencesDialog::onMouseDown(MouseEvent& e) {
    const float mx = e.x, my = e.y;

    // Outside body → cancel.
    if (mx < m_body.x || mx > m_body.x + m_body.w ||
        my < m_body.y || my > m_body.y + m_body.h) {
        close(PreferencesResult::Cancel);
        return true;
    }

    // Close / OK / Cancel buttons — handled inline, not as widgets.
    const Rect cb = closeButtonRect(m_body);
    if (mx >= cb.x && mx <= cb.x + cb.w && my >= cb.y && my <= cb.y + cb.h) {
        close(PreferencesResult::Cancel);
        return true;
    }
    const Rect okR = okButtonRect(m_body);
    if (mx >= okR.x && mx <= okR.x + okR.w && my >= okR.y && my <= okR.y + okR.h) {
        close(PreferencesResult::OK);
        return true;
    }
    const Rect cancelR = cancelButtonRect(m_body);
    if (mx >= cancelR.x && mx <= cancelR.x + cancelR.w &&
        my >= cancelR.y && my <= cancelR.y + cancelR.h) {
        close(PreferencesResult::Cancel);
        return true;
    }

    // Tab strip — TabView hit-tests itself and fires m_onActivated,
    // which updates m_tab + closes any dangling popup.
    const Rect& tabBounds = m_tabStrip.bounds();
    if (mx >= tabBounds.x && mx < tabBounds.x + tabBounds.w &&
        my >= tabBounds.y && my < tabBounds.y + tabBounds.h) {
        forwardMouse(&m_tabStrip, e, &Widget::dispatchMouseDown);
        return true;
    }

    // Content area — forward to the widget under the pointer.
    Widget* w = findWidgetAt(mx, my);
    if (w) forwardMouse(w, e, &Widget::dispatchMouseDown);
    return true;
}

bool FwPreferencesDialog::onMouseUp(MouseEvent& e) {
    // Gesture follow-up: the captured widget (if any) must receive the
    // up event to finish the click SM. Without this, FwDropDown never
    // sees its release and never fires onClick.
    Widget* cap = Widget::capturedWidget();
    if (cap) forwardMouse(cap, e, &Widget::dispatchMouseUp);
    else {
        // No capture — forward to whatever's under the pointer so
        // click-less up events don't dangle (rare, e.g. after a drag
        // into the dialog from outside).
        Widget* w = findWidgetAt(e.x, e.y);
        if (w) forwardMouse(w, e, &Widget::dispatchMouseUp);
    }
    return true;
}

bool FwPreferencesDialog::onMouseMove(MouseMoveEvent& e) {
    // Captured widget always wins (drag-over-bounds still routes there).
    Widget* cap = Widget::capturedWidget();
    if (cap) {
        forwardMouseMove(cap, e);
        return true;
    }
    // Tab strip hover tracking.
    {
        MouseMoveEvent copy = e;
        forwardMouseMove(&m_tabStrip, copy);
    }
    // Content-area widgets — each bounds-checks internally for hover.
    for (Widget* w : visibleWidgets()) {
        if (!w) continue;
        MouseMoveEvent copy = e;
        forwardMouseMove(w, copy);
    }
    return true;
}

bool FwPreferencesDialog::onKey(KeyEvent& e) {
    if (e.consumed) return false;
    if (e.key == Key::Escape) { close(PreferencesResult::Cancel); return true; }
    if (e.key == Key::Enter)  { close(PreferencesResult::OK);     return true; }
    return true;   // modal swallows
}

void FwPreferencesDialog::onDismiss() {
    m_handle.detach_noRemove();
    if (m_closing) { m_closing = false; return; }
    if (m_onResult) m_onResult(m_result);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
