#pragma once
// PreferencesDialog — UI v2 version, widget-driven.
//
// Runs on LayerStack::Modal via an OverlayEntry. Internally builds a
// tree of fw2 widgets (FwDropDown for every dropdown, FwCheckbox for
// each MIDI port, v1 Widget::capturedWidget() for gesture follow-up)
// and hand-dispatches events from the overlay callbacks. Each tab's
// widgets are laid out + rendered in paintBody; popups (owned by
// FwDropDown) push themselves onto LayerStack::Overlay so they draw
// above the dialog body and dismiss on outside-click automatically.
//
// Public API (open/close/isOpen/state/setOnResult) matches the earlier
// immediate-mode migration, so App wiring is unchanged.

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Checkbox.h"
#include "ui/framework/v2/TabView.h"

#include "audio/AudioEngine.h"
#include "audio/ClipEngine.h"   // QuantizeMode
#include "midi/MidiPort.h"
#include "midi/MidiEngine.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

enum class PreferencesResult { OK, Cancel };

class FwPreferencesDialog {
public:
    struct State {
        int selectedOutputDevice = -1;
        int selectedInputDevice  = -1;
        double sampleRate        = 44100.0;
        int bufferSize           = 256;
        audio::QuantizeMode defaultLaunchQuantize = audio::QuantizeMode::NextBar;
        audio::QuantizeMode defaultRecordQuantize = audio::QuantizeMode::NextBar;
        std::vector<int> enabledMidiInputs;
        std::vector<int> enabledMidiOutputs;

        // Metronome
        float metronomeVolume       = 0.7f;
        int   metronomeMode         = 0;
        int   countInBars           = 0;
        int   metronomeVisualStyle  = 0;

        // Theme
        float fontScale             = 1.0f;
    };

    using ResultCallback = std::function<void(PreferencesResult)>;

    FwPreferencesDialog();
    ~FwPreferencesDialog();

    FwPreferencesDialog(const FwPreferencesDialog&)            = delete;
    FwPreferencesDialog& operator=(const FwPreferencesDialog&) = delete;

    void open(State initialState, audio::AudioEngine* engine,
              midi::MidiEngine* midiEngine);
    void close(PreferencesResult r = PreferencesResult::Cancel);
    bool isOpen() const { return m_handle.active(); }

    const State& state() const { return m_state; }
    void setOnResult(ResultCallback cb) { m_onResult = std::move(cb); }

    // Paint hook — invoked by the overlay-entry paint closure. Public
    // so the lambda set up in open() can call straight through.
    void paintBody(UIContext& ctx);

private:
    // ─── Chrome metrics ─────────────────────────────────────────────
    static constexpr float kTitleBarHeight  = 32.0f;
    static constexpr float kFooterHeight    = 40.0f;
    static constexpr float kPadding         = 8.0f;
    static constexpr float kCloseButtonSize = 24.0f;

    // ─── Geometry helpers ──────────────────────────────────────────
    Rect bodyRect(const UIContext& ctx) const;
    Rect closeButtonRect(const Rect& body) const;
    Rect okButtonRect(const Rect& body) const;
    Rect cancelButtonRect(const Rect& body) const;

    // ─── Setup ─────────────────────────────────────────────────────
    void refreshDevices();
    void configureStaticDropdowns();   // one-time items that don't depend on State
    void syncDropdownsToState();       // per-open: items + selections
    void rebuildMidiChecks();          // per-open: recreate FwCheckbox widgets

    // ─── Paint ─────────────────────────────────────────────────────
    // Tab strip is rendered by m_tabStrip (a TabView child) — the
    // dialog lays it out + forwards events inside onMouseDown/Move.
    void paintFooter(UIContext& ctx);
    void layoutAndRenderAudioTab(UIContext& ctx, Rect content);
    void layoutAndRenderMidiTab(UIContext& ctx, Rect content);
    void layoutAndRenderDefaultsTab(UIContext& ctx, Rect content);
    void layoutAndRenderMetronomeTab(UIContext& ctx, Rect content);
    void layoutAndRenderThemeTab(UIContext& ctx, Rect content);
    void drawLabeledRow(UIContext& ctx, const char* label,
                        Rect labelArea, float textScale);

    // ─── Event dispatch ────────────────────────────────────────────
    std::vector<Widget*> visibleWidgets();
    Widget* findWidgetAt(float sx, float sy);
    void    forwardMouse(Widget* w, MouseEvent& e,
                         bool (Widget::*fn)(MouseEvent&));
    void    forwardMouseMove(Widget* w, MouseMoveEvent& e);

    // ─── Overlay entry callbacks ───────────────────────────────────
    bool onMouseDown(MouseEvent& e);
    bool onMouseUp(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onDismiss();

    // ─── State ──────────────────────────────────────────────────────
    State m_state;
    audio::AudioEngine*  m_engine     = nullptr;
    midi::MidiEngine*    m_midiEngine = nullptr;

    std::vector<audio::AudioDevice> m_audioDevices;
    std::vector<std::string>         m_midiInputPorts;
    std::vector<std::string>         m_midiOutputPorts;
    // Map dropdown index → device.id (devices with 0 channels are
    // filtered per output/input).
    std::vector<int> m_outputDeviceIds;
    std::vector<int> m_inputDeviceIds;

    int   m_tab         = 0;
    float m_preferredW  = 540.0f;
    float m_preferredH  = 460.0f;
    Rect  m_body{};

    // ─── Widgets ───────────────────────────────────────────────────
    // TabView owns the strip (click routing, hover, active indicator).
    // We pass nullptr content to each tab — the dialog paints the
    // active tab's dropdowns itself via its per-tab render methods.
    TabView    m_tabStrip;

    FwDropDown m_outputDD;
    FwDropDown m_inputDD;
    FwDropDown m_sampleRateDD;
    FwDropDown m_bufferSizeDD;
    FwDropDown m_launchQDD;
    FwDropDown m_recordQDD;
    FwDropDown m_metroModeDD;
    FwDropDown m_countInDD;
    FwDropDown m_metroVolumeDD;
    FwDropDown m_vizStyleDD;
    FwDropDown m_fontScaleDD;

    // MIDI port checkboxes — regenerated per-open since the port list
    // changes with hardware. unique_ptr keeps widget addresses stable
    // across vector reallocations.
    std::vector<std::unique_ptr<FwCheckbox>> m_midiInputChecks;
    std::vector<std::unique_ptr<FwCheckbox>> m_midiOutputChecks;

    PreferencesResult m_result  = PreferencesResult::Cancel;
    ResultCallback    m_onResult;
    OverlayHandle     m_handle;
    bool              m_closing = false;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
