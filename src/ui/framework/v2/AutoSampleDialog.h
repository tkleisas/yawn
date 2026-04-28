#pragma once
//
// fw2::AutoSampleDialog — modal dialog driving an audio::AutoSampleWorker.
// Two visual modes (mirroring FwExportDialog's pattern):
//
//   • Config mode  — form widgets for capture name / MIDI port + channel /
//                    audio input + channels / note range + step /
//                    velocity layers / note + release timing / replace
//                    + trim toggles, plus a collapsible "Advanced"
//                    disclosure (silence threshold, pre-roll).
//                    Footer: [Test Note] [Cancel] [Capture]
//
//   • Running mode — FwProgressBar + "Now: <note name> vel <N>" line.
//                    Footer collapses to [Stop].
//
// On Capture:
//   1. Build the AutoSampleConfig from the form.
//   2. If <samples>/<captureName>/ exists, pop a confirm dialog:
//      "Folder '<name>' exists. Overwrite contents?" [Overwrite] [Cancel]
//   3. On Overwrite (or no collision), call AutoSampleWorker::start.
//   4. tick() drives the worker each frame and updates the progress bar.
//
// On Test Note:
//   Sends Note-On C4 vel 100 to the configured MIDI port, schedules
//   Note-Off ~500 ms later via a small wall-clock timer in tick().
//   Useful for verifying port wiring before committing to a full run.
//
// Lifetime: value-typed App member, like FwExportDialog. Open call
// supplies the Context (target Multisampler, project folder, default
// capture name). Result callback fires on Done / Aborted / Error so
// the host can clean up (e.g. refresh the Multisampler panel).

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/framework/v2/TextInput.h"
#include "ui/framework/v2/ProgressBar.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/Meter.h"
#include "ui/framework/v2/Dialog.h"

#include "audio/AutoSampler.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace yawn { namespace audio { class AudioEngine; } }
namespace yawn { namespace midi  { class MidiEngine;  } }
namespace yawn { namespace instruments { class Multisampler; } }

namespace yawn {
namespace ui {
namespace fw2 {

class FwAutoSampleDialog {
public:
    // Everything the dialog needs that isn't a form field. Filled by
    // the caller (App) right before open().
    struct Context {
        audio::AudioEngine*           engine = nullptr;
        midi::MidiEngine*             midi   = nullptr;
        instruments::Multisampler*    target = nullptr;
        std::filesystem::path         samplesRoot;        // <project>.yawn/samples
        std::string                   defaultCaptureName; // e.g. "midi_1_capture"
    };

    enum class Result { Done, Cancelled, Error };
    using ResultCallback = std::function<void(Result)>;

    FwAutoSampleDialog();
    ~FwAutoSampleDialog();

    FwAutoSampleDialog(const FwAutoSampleDialog&)            = delete;
    FwAutoSampleDialog& operator=(const FwAutoSampleDialog&) = delete;

    void open(const Context& ctx);
    void close();
    bool isOpen() const { return m_handle.active(); }

    // Per-frame tick — drives the worker, syncs the progress bar,
    // and resolves the test-note Note-Off timer.
    void tick();

    // Forward an SDL TEXT_INPUT payload into whichever child text
    // field is currently editing. App routes SDL_EVENT_TEXT_INPUT here
    // while the dialog is open so the user can actually type into the
    // capture-name field. No-op when nothing is editing (keeps caller
    // logic in App simple — "always forward, dialog filters").
    void takeTextInput(const std::string& text);

    // True while a child text-input has focus + edit mode. App polls
    // this to decide whether to keep SDL text-input mode active.
    bool isEditingText() const;

    void setOnResult(ResultCallback cb) { m_onResult = std::move(cb); }

private:
    // Chrome metrics
    static constexpr float kTitleBarH = 32.0f;
    static constexpr float kFooterH   = 44.0f;
    static constexpr float kPad       = 10.0f;
    static constexpr float kRowH      = 28.0f;
    static constexpr float kLabelW    = 110.0f;
    // Dialog body width — set so the two toggles in the "replace /
    // trim" row both fit their labels in the Button variant. At
    // 14-px theme font, "Replace existing zones" is ~170 px and the
    // half-row width is `(width - 2*pad - labelW) / 2`. 600 px gives
    // each toggle ~230 px → comfortable fit with breathing room for
    // localised strings later.
    static constexpr float kPreferredW = 600.0f;
    static constexpr float kPreferredH = 620.0f;            // +80 for level/meter row
    static constexpr float kPreferredHCollapsed = 540.0f;   // +80 for level/meter row

    // ── Setup ───────────────────────────────────────────────────────
    void configureWidgets();
    void populatePortDropdown();
    void populateInputDropdown();
    void syncWidgetsToConfig();
    void readConfigFromWidgets();
    int  totalSampleCount() const;
    double estimatedSecondsTotal() const;

    // ── Geometry ────────────────────────────────────────────────────
    Rect bodyRect(const UIContext& ctx) const;

    // ── Paint ───────────────────────────────────────────────────────
    void paintBody(UIContext& ctx);
    void paintConfigMode(UIContext& ctx, const Rect& body);
    void paintRunningMode(UIContext& ctx, const Rect& body);

    void layoutConfigWidgets(const Rect& body, UIContext& ctx);
    void layoutRunningWidgets(const Rect& body, UIContext& ctx);

    // ── Event dispatch ──────────────────────────────────────────────
    Widget* findWidgetAt(float sx, float sy);
    bool onMouseDown(MouseEvent& e);
    bool onMouseUp(MouseEvent& e);
    bool onMouseMove(MouseMoveEvent& e);
    bool onKey(KeyEvent& e);
    void onDismiss();

    // ── Action handlers ─────────────────────────────────────────────
    void onCaptureClicked();
    void onTestNoteClicked();
    void onStopClicked();
    void onWorkerFinished(audio::AutoSampleStatus::Phase phase);

    // ── State ───────────────────────────────────────────────────────
    Context                              m_ctx{};
    audio::AutoSampleConfig              m_cfg;
    std::unique_ptr<audio::AutoSampleWorker> m_worker;
    bool                                 m_running = false;
    bool                                 m_advancedExpanded = false;

    // Test-note timer: when set, we'll send Note-Off when steady_clock
    // exceeds m_testNoteOffAt. -1 = no test note pending.
    std::chrono::steady_clock::time_point m_testNoteOffAt{};
    bool                                  m_testNotePending = false;
    int                                   m_testNotePort   = 0;
    int                                   m_testNoteChannel = 0;

    bool            m_pendingFolderConfirm = false;

    // Test-Note hold state. The button toggles between Note-On (held)
    // and Note-Off so the user can preview at length while watching
    // the input meter. m_testNoteHeld == true ⇒ next click sends Off.
    bool                                  m_testNoteHeld    = false;

    // Smoothed peak values for the VU display. AudioEngine reports raw
    // per-block peaks; we apply UI-side decay (~3 dB/100ms) so the
    // meter looks like a real VU rather than flickering instantly to
    // every block's peak.
    float                                 m_meterPeakL = 0.0f;
    float                                 m_meterPeakR = 0.0f;
    std::chrono::steady_clock::time_point m_lastMeterTick{};

    // Widgets — config mode
    FwTextInput   m_nameInput;
    FwDropDown    m_portDD;
    FwDropDown    m_channelDD;
    FwDropDown    m_inputDD;
    FwDropDown    m_channelsDD;          // mono/stereo
    FwKnob        m_levelKnob;            // recording-level (-24..+24 dB)
    FwMeter       m_inputMeter;           // live VU on the selected input
    FwNumberInput m_lowNoteInput;
    FwNumberInput m_highNoteInput;
    FwDropDown    m_stepDD;
    FwDropDown    m_velLayersDD;
    FwNumberInput m_noteLengthInput;
    FwNumberInput m_releaseTailInput;
    FwToggle      m_replaceToggle;
    FwToggle      m_trimToggle;
    FwButton      m_advancedBtn;          // expand/collapse
    FwNumberInput m_silenceThreshInput;
    FwNumberInput m_preRollInput;

    // Footer buttons — config mode
    FwButton      m_testNoteBtn;
    FwButton      m_cancelBtn;
    FwButton      m_captureBtn;

    // Running mode
    FwProgressBar m_progressBar;
    FwButton      m_stopBtn;

    Rect          m_body{};
    OverlayHandle m_handle;
    ResultCallback m_onResult;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
