// FwAutoSampleDialog — the modal that drives audio::AutoSampleWorker.
// Mirrors FwExportDialog's two-mode (config / running) pattern.

#include "AutoSampleDialog.h"

#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"

#include "audio/AudioEngine.h"
#include "midi/MidiEngine.h"
#include "instruments/Multisampler.h"
#include "util/Logger.h"
#include "util/NoteNames.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace yawn {
namespace ui {
namespace fw2 {

// Send a Note-Off if a held test note is still ringing. Used by
// close(), onDismiss, and onWorkerFinished so we never leave a stuck
// note on the synth when the dialog goes away.
static void sendOffIfHeld(midi::MidiEngine* mid, bool& held,
                           int port, int channel) {
    if (!held || !mid) return;
    midi::MidiBuffer buf;
    midi::MidiMessage msg;
    msg.type        = midi::MidiMessage::Type::NoteOff;
    msg.channel     = static_cast<uint8_t>(channel);
    msg.note        = 60;
    msg.velocity    = 0;
    msg.frameOffset = 0;
    buf.addMessage(msg);
    mid->sendToOutput(port, buf);
    held = false;
}

// ───────────────────────────────────────────────────────────────────
// Lifetime
// ───────────────────────────────────────────────────────────────────

FwAutoSampleDialog::FwAutoSampleDialog() {
    configureWidgets();
}

FwAutoSampleDialog::~FwAutoSampleDialog() {
    if (m_handle.active()) m_handle.remove();
}

// ───────────────────────────────────────────────────────────────────
// Public API
// ───────────────────────────────────────────────────────────────────

void FwAutoSampleDialog::open(const Context& ctx) {
    if (m_handle.active()) m_handle.remove();

    m_ctx = ctx;
    m_running = false;
    m_advancedExpanded = false;
    m_testNotePending = false;
    m_testNoteHeld = false;
    m_testNoteBtn.setLabel("Test Note");
    m_meterPeakL = 0.0f;
    m_meterPeakR = 0.0f;
    m_lastMeterTick = {};

    // Reset config to defaults; override capture name from context.
    m_cfg = audio::AutoSampleConfig{};
    if (!ctx.defaultCaptureName.empty()) {
        m_cfg.captureName = ctx.defaultCaptureName;
    }

    populatePortDropdown();
    populateInputDropdown();
    syncWidgetsToConfig();

    // Auto-focus the capture-name field so the user can immediately
    // overwrite or edit the auto-derived default. Cursor at the end of
    // the existing text matches the typical "rename" UX (Windows
    // Explorer F2, macOS Finder Return).
    m_nameInput.setCursor(static_cast<int>(m_nameInput.text().size()));
    m_nameInput.beginEdit();

    UIContext& uctx = UIContext::global();
    if (!uctx.layerStack) return;

    OverlayEntry entry;
    entry.debugName             = "AutoSampleDialog";
    entry.bounds                = uctx.viewport;
    entry.modal                 = true;
    entry.dismissOnOutsideClick = false;
    entry.paint       = [this](UIContext& c)      { this->paintBody(c); };
    entry.onMouseDown = [this](MouseEvent& e)     { return this->onMouseDown(e); };
    entry.onMouseUp   = [this](MouseEvent& e)     { return this->onMouseUp(e);   };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return this->onMouseMove(e); };
    entry.onKey       = [this](KeyEvent& e)       { return this->onKey(e); };
    entry.onDismiss   = [this]()                  { this->onDismiss(); };

    m_handle = uctx.layerStack->push(OverlayLayer::Modal, std::move(entry));
}

void FwAutoSampleDialog::close() {
    if (!m_handle.active()) return;
    sendOffIfHeld(m_ctx.midi, m_testNoteHeld,
                   m_testNotePort, m_testNoteChannel);
    m_handle.remove();
}

void FwAutoSampleDialog::tick() {
    // Advance the worker if it's running. The worker handles its own
    // state machine; we just need to call tick() each frame and read
    // status afterwards for the progress bar.
    if (m_worker && m_worker->isActive()) {
        m_worker->tick();
        const auto& s = m_worker->status();
        if (s.totalSamples > 0) {
            const float frac = static_cast<float>(s.completedSamples) /
                               static_cast<float>(s.totalSamples);
            m_progressBar.setValue(std::clamp(frac, 0.0f, 1.0f));
        }
    }

    // Live VU update — poll the engine's per-channel input peak
    // (atomic exchange-to-zero, so each tick reads "max since last
    // tick"). Apply UI-side exponential decay so the meter glides
    // back to silence rather than flickering instantly between
    // blocks. Decay rate is chosen so a -infdB→0dB→silence transition
    // takes about 250 ms — close to a hardware VU's ballistics.
    //
    // We also multiply the peak by the recording-level gain so the
    // displayed bar reflects the post-gain level the file will
    // actually be saved at — that's the loop the user wants to
    // close ("dial gain until the meter just kisses 0 dBFS").
    if (m_ctx.engine && !m_running) {
        const auto now = std::chrono::steady_clock::now();
        if (m_lastMeterTick.time_since_epoch().count() == 0)
            m_lastMeterTick = now;
        const auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastMeterTick).count();
        m_lastMeterTick = now;
        // Decay coefficient: per-millisecond multiplier such that we
        // drop ~60 dB over 250 ms. exp(ln(1e-3) / 250) ≈ 0.973.
        const float decayPerMs = 0.973f;
        const float decay = std::pow(decayPerMs, static_cast<float>(dtMs));
        m_meterPeakL *= decay;
        m_meterPeakR *= decay;

        const int baseCh = m_inputDD.selectedIndex();
        const int audioCh = (m_channelsDD.selectedIndex() == 0) ? 1 : 2;
        const float gain = std::pow(10.0f, m_levelKnob.value() / 20.0f);
        const float pkL = m_ctx.engine->consumeInputPeak(baseCh) * gain;
        m_meterPeakL = std::max(m_meterPeakL, pkL);
        if (audioCh > 1) {
            const float pkR = m_ctx.engine->consumeInputPeak(baseCh + 1) * gain;
            m_meterPeakR = std::max(m_meterPeakR, pkR);
        } else {
            // Mono: mirror left → right so the meter shows a single bar
            // rather than confusing the user with an empty R column.
            m_meterPeakR = m_meterPeakL;
        }
        m_inputMeter.setPeak(m_meterPeakL, m_meterPeakR);
    }

    // Test-note Note-Off timer (legacy 500-ms one-shot path — only
    // armed when the user clicked "Test Note" without holding it as
    // a toggle, which we no longer do but the field is kept for
    // forward compatibility / unsolicited Note-Offs).
    if (m_testNotePending) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_testNoteOffAt) {
            m_testNotePending = false;
            if (m_ctx.midi) {
                midi::MidiBuffer buf;
                midi::MidiMessage msg;
                msg.type = midi::MidiMessage::Type::NoteOff;
                msg.channel = static_cast<uint8_t>(m_testNoteChannel);
                msg.note = 60;        // C4
                msg.velocity = 0;
                msg.frameOffset = 0;
                buf.addMessage(msg);
                m_ctx.midi->sendToOutput(m_testNotePort, buf);
            }
        }
    }
}

void FwAutoSampleDialog::takeTextInput(const std::string& text) {
    // The capture-name field is the only text input in the dialog.
    // Forward the SDL TEXT_INPUT payload — FwTextInput::takeTextInput
    // is a no-op when the field isn't editing, so this is safe to
    // call unconditionally.
    if (m_running) return;
    m_nameInput.takeTextInput(text);
}

bool FwAutoSampleDialog::isEditingText() const {
    return m_nameInput.isEditing();
}

// ───────────────────────────────────────────────────────────────────
// Widget setup
// ───────────────────────────────────────────────────────────────────

void FwAutoSampleDialog::configureWidgets() {
    // Capture name: free-form text input.
    m_nameInput.setPlaceholder("capture_name");

    // Channel: 1..16 dropdown — store 0-based internally, display 1-based.
    {
        std::vector<std::string> items;
        items.reserve(16);
        for (int i = 1; i <= 16; ++i) items.push_back(std::to_string(i));
        m_channelDD.setItems(items);
        m_channelDD.setSelectedIndex(0);
    }

    // Channels: mono / stereo.
    m_channelsDD.setItems(std::vector<std::string>{"Mono", "Stereo"});
    m_channelsDD.setSelectedIndex(1);

    // Note step presets — common semitone intervals used by sample
    // libraries. M3 (4 semis) is the standard tradeoff.
    m_stepDD.setItems(std::vector<std::string>{
        "Every semitone (1)",
        "Every M2 (2)",
        "Every m3 (3)",
        "Every M3 (4)",
        "Every TT (6)",
        "Every octave (12)"
    });
    m_stepDD.setSelectedIndex(3);  // M3 default

    // Velocity layer presets.
    m_velLayersDD.setItems(std::vector<std::string>{
        "1 layer",
        "2 layers",
        "3 layers",
        "4 layers (default)",
        "8 layers"
    });
    m_velLayersDD.setSelectedIndex(3);  // 4 layers default

    // Note range — MIDI 0..127 with note-name display.
    auto noteName = [](float v) {
        return ::yawn::util::midiNoteName(static_cast<int>(v));
    };
    m_lowNoteInput.setRange(0, 127);
    m_lowNoteInput.setStep(1);
    m_lowNoteInput.setValueFormatter(noteName);
    m_highNoteInput.setRange(0, 127);
    m_highNoteInput.setStep(1);
    m_highNoteInput.setValueFormatter(noteName);

    // Timing — seconds with one decimal.
    m_noteLengthInput.setRange(0.1f, 30.0f);
    m_noteLengthInput.setStep(0.1f);
    m_noteLengthInput.setSuffix("s");
    m_noteLengthInput.setFormat("%.1f");

    m_releaseTailInput.setRange(0.0f, 30.0f);
    m_releaseTailInput.setStep(0.1f);
    m_releaseTailInput.setSuffix("s");
    m_releaseTailInput.setFormat("%.1f");

    m_replaceToggle.setLabel("Replace existing zones");
    m_replaceToggle.setState(true);

    m_trimToggle.setLabel("Trim leading silence");
    m_trimToggle.setState(true);

    // Recording-level knob: ±24 dB software gain applied to captured
    // samples before save. Bipolar so the user can see "0 dB = no
    // change" at the centre detent.
    m_levelKnob.setRange(-24.0f, 24.0f);
    m_levelKnob.setDefaultValue(0.0f);
    m_levelKnob.setBipolar(true);
    m_levelKnob.setLabel("Level");
    m_levelKnob.setValueFormatter([](float v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
        return std::string(buf);
    });

    // Advanced fields.
    m_silenceThreshInput.setRange(-100.0f, 0.0f);
    m_silenceThreshInput.setStep(1.0f);
    m_silenceThreshInput.setSuffix("dB");
    m_silenceThreshInput.setFormat("%.0f");

    m_preRollInput.setRange(0.0f, 5.0f);
    m_preRollInput.setStep(0.1f);
    m_preRollInput.setSuffix("s");
    m_preRollInput.setFormat("%.1f");

    // Disclosure indicator. Use UCN escapes (\uXXXX) rather than the
    // raw glyph in the source — MSVC's source-charset defaults to the
    // system codepage (CP1253 here, since the user's locale is Greek),
    // so a literal "▸" in the .cpp would be re-decoded as Greek letters
    // before u8 transcoding, producing garbage like "βY". UCN escapes
    // resolve to a fixed Unicode codepoint regardless of source
    // encoding, then u8"..." emits canonical UTF-8 bytes.
    //
    //   U+25B8 ▸  collapsed — "click to reveal"
    //   U+25BE ▾  expanded  — "click to hide"
    m_advancedBtn.setLabel(u8"\u25B8 Advanced");
    m_advancedBtn.setOnClick([this]() {
        m_advancedExpanded = !m_advancedExpanded;
        m_advancedBtn.setLabel(m_advancedExpanded ? u8"\u25BE Advanced"
                                                  : u8"\u25B8 Advanced");
    });

    // Footer buttons.
    m_testNoteBtn.setLabel("Test Note");
    m_testNoteBtn.setOnClick([this]() { onTestNoteClicked(); });

    m_cancelBtn.setLabel("Cancel");
    m_cancelBtn.setOnClick([this]() {
        if (m_onResult) m_onResult(Result::Cancelled);
        close();
    });

    m_captureBtn.setLabel("Capture");
    m_captureBtn.setOnClick([this]() { onCaptureClicked(); });

    m_stopBtn.setLabel("Stop");
    m_stopBtn.setOnClick([this]() { onStopClicked(); });

    m_progressBar.setMinLength(360.0f);
}

void FwAutoSampleDialog::populatePortDropdown() {
    std::vector<std::string> items;
    if (m_ctx.midi) {
        for (int i = 0; i < m_ctx.midi->openOutputPortCount(); ++i) {
            std::string nm = m_ctx.midi->openOutputPortName(i);
            if (nm.empty()) nm = "Port " + std::to_string(i + 1);
            items.push_back(std::move(nm));
        }
    }
    if (items.empty()) items.push_back("(no MIDI outputs open)");
    m_portDD.setItems(items);
    m_portDD.setSelectedIndex(0);
}

void FwAutoSampleDialog::populateInputDropdown() {
    // Audio inputs aren't named at the engine level — we just expose
    // them as "Input N" up to the configured input channel count.
    int inCh = m_ctx.engine ? m_ctx.engine->config().inputChannels : 0;
    std::vector<std::string> items;
    for (int i = 0; i < inCh; ++i) {
        items.push_back("Input " + std::to_string(i + 1));
    }
    if (items.empty()) items.push_back("(no audio input)");
    m_inputDD.setItems(items);
    m_inputDD.setSelectedIndex(0);
}

void FwAutoSampleDialog::syncWidgetsToConfig() {
    m_nameInput.setText(m_cfg.captureName);
    m_lowNoteInput.setValue(static_cast<float>(m_cfg.lowNote),
                             ValueChangeSource::Programmatic);
    m_highNoteInput.setValue(static_cast<float>(m_cfg.highNote),
                              ValueChangeSource::Programmatic);
    m_noteLengthInput.setValue(m_cfg.noteLengthSec,
                                ValueChangeSource::Programmatic);
    m_releaseTailInput.setValue(m_cfg.releaseTailSec,
                                 ValueChangeSource::Programmatic);
    m_silenceThreshInput.setValue(m_cfg.silenceThresholdDb,
                                    ValueChangeSource::Programmatic);
    m_preRollInput.setValue(m_cfg.preRollSec,
                             ValueChangeSource::Programmatic);
    m_replaceToggle.setState(m_cfg.replaceExistingZones,
                              ValueChangeSource::Programmatic);
    m_trimToggle.setState(m_cfg.trimLeadingSilence,
                           ValueChangeSource::Programmatic);
    m_levelKnob.setValue(m_cfg.recordingLevelDb,
                          ValueChangeSource::Programmatic);
    m_channelDD.setSelectedIndex(m_cfg.midiChannel);
    m_channelsDD.setSelectedIndex(m_cfg.audioChannels == 1 ? 0 : 1);
}

void FwAutoSampleDialog::readConfigFromWidgets() {
    m_cfg.captureName        = audio::sanitizeCaptureName(m_nameInput.text());
    m_cfg.midiOutputPortIndex = std::max(0, m_portDD.selectedIndex());
    m_cfg.midiChannel        = std::clamp(m_channelDD.selectedIndex(), 0, 15);
    m_cfg.audioInputChannel  = std::max(0, m_inputDD.selectedIndex());
    m_cfg.audioChannels      = m_channelsDD.selectedIndex() == 0 ? 1 : 2;
    m_cfg.lowNote            = static_cast<int>(m_lowNoteInput.value());
    m_cfg.highNote           = static_cast<int>(m_highNoteInput.value());

    static const int stepValues[]   = {1, 2, 3, 4, 6, 12};
    static const int layerCounts[]  = {1, 2, 3, 4, 8};
    int sIdx = std::clamp(m_stepDD.selectedIndex(), 0, 5);
    int lIdx = std::clamp(m_velLayersDD.selectedIndex(), 0, 4);
    m_cfg.noteStep        = stepValues[sIdx];
    m_cfg.velocityLayers  = audio::velocityLayerPreset(layerCounts[lIdx]);
    m_cfg.noteLengthSec   = m_noteLengthInput.value();
    m_cfg.releaseTailSec  = m_releaseTailInput.value();
    m_cfg.replaceExistingZones = m_replaceToggle.state();
    m_cfg.trimLeadingSilence   = m_trimToggle.state();
    m_cfg.silenceThresholdDb   = m_silenceThreshInput.value();
    m_cfg.preRollSec      = m_preRollInput.value();
    m_cfg.recordingLevelDb = m_levelKnob.value();
    m_cfg.captureFolder   = m_ctx.samplesRoot / m_cfg.captureName;
}

int FwAutoSampleDialog::totalSampleCount() const {
    int notesPerOctave = 0;
    static const int stepValues[] = {1, 2, 3, 4, 6, 12};
    static const int layerCounts[] = {1, 2, 3, 4, 8};
    int sIdx = std::clamp(m_stepDD.selectedIndex(), 0, 5);
    int lIdx = std::clamp(m_velLayersDD.selectedIndex(), 0, 4);
    int step = stepValues[sIdx];
    int layers = layerCounts[lIdx];
    int low = static_cast<int>(m_lowNoteInput.value());
    int high = static_cast<int>(m_highNoteInput.value());
    if (low > high) std::swap(low, high);
    int noteCount = (high - low) / step + 1;
    (void)notesPerOctave;
    return std::max(0, noteCount * layers);
}

double FwAutoSampleDialog::estimatedSecondsTotal() const {
    const double perSample = m_noteLengthInput.value() +
                              m_releaseTailInput.value() +
                              m_preRollInput.value();
    return totalSampleCount() * perSample;
}

// ───────────────────────────────────────────────────────────────────
// Geometry
// ───────────────────────────────────────────────────────────────────

Rect FwAutoSampleDialog::bodyRect(const UIContext& ctx) const {
    const float w = kPreferredW;
    const float h = m_running ? 220.0f
                              : (m_advancedExpanded ? kPreferredH
                                                    : kPreferredHCollapsed);
    const Rect& vp = ctx.viewport;
    const float x = vp.x + (vp.w - w) * 0.5f;
    const float y = vp.y + (vp.h - h) * 0.5f;
    return {x, y, w, h};
}

// ───────────────────────────────────────────────────────────────────
// Layout
// ───────────────────────────────────────────────────────────────────

void FwAutoSampleDialog::layoutConfigWidgets(const Rect& body, UIContext& ctx) {
    const float fieldX = body.x + kPad + kLabelW;
    const float fieldW = body.w - kPad * 2 - kLabelW;
    const float halfW  = (fieldW - kPad) * 0.5f;
    float y = body.y + kTitleBarH + kPad;

    auto place = [&](Widget& w, float x, float yy, float ww, float hh) {
        w.layout({x, yy, ww, hh}, ctx);
    };

    // Capture name
    place(m_nameInput, fieldX, y, fieldW, kRowH - 4);
    y += kRowH;

    // Port + channel (split row)
    place(m_portDD,    fieldX,                y, halfW * 1.4f, kRowH - 4);
    place(m_channelDD, fieldX + halfW * 1.4f + kPad, y,
          fieldW - halfW * 1.4f - kPad, kRowH - 4);
    y += kRowH;

    // Audio input + channels
    place(m_inputDD,    fieldX,                y, halfW * 1.4f, kRowH - 4);
    place(m_channelsDD, fieldX + halfW * 1.4f + kPad, y,
          fieldW - halfW * 1.4f - kPad, kRowH - 4);
    y += kRowH;

    // Recording level + VU meter row. The knob shares its row with the
    // meter so the user reads "this gain produces this level" left-to-
    // right; the row is taller than other form rows (kMeterRowH) to
    // give the knob disc and meter bars enough vertical real estate.
    {
        const float kMeterRowH = 64.0f;
        const float knobW = 110.0f;
        const float meterX = fieldX + knobW + kPad;
        const float meterW = fieldW - knobW - kPad;
        place(m_levelKnob,   fieldX, y,        knobW,  kMeterRowH);
        place(m_inputMeter,  meterX, y + 8.0f, meterW, kMeterRowH - 16.0f);
        y += kMeterRowH + 4.0f;
    }

    // Note range (low / high)
    place(m_lowNoteInput,  fieldX,                  y, halfW, kRowH - 4);
    place(m_highNoteInput, fieldX + halfW + kPad,   y, halfW, kRowH - 4);
    y += kRowH;

    // Step + velocity layers
    place(m_stepDD,      fieldX,                  y, halfW, kRowH - 4);
    place(m_velLayersDD, fieldX + halfW + kPad,   y, halfW, kRowH - 4);
    y += kRowH;

    // Note length + release tail
    place(m_noteLengthInput,  fieldX,                y, halfW, kRowH - 4);
    place(m_releaseTailInput, fieldX + halfW + kPad, y, halfW, kRowH - 4);
    y += kRowH;

    // Replace + trim toggles
    place(m_replaceToggle, fieldX,                  y, halfW, kRowH - 4);
    place(m_trimToggle,    fieldX + halfW + kPad,   y, halfW, kRowH - 4);
    y += kRowH;

    // Advanced disclosure button.
    place(m_advancedBtn, fieldX, y, 110.0f, kRowH - 4);
    y += kRowH;

    if (m_advancedExpanded) {
        place(m_silenceThreshInput, fieldX, y, halfW, kRowH - 4);
        place(m_preRollInput,       fieldX + halfW + kPad, y,
              halfW, kRowH - 4);
        y += kRowH;
    }

    // Footer
    const float footerY = body.y + body.h - kFooterH + 6;
    const float btnW = 96.0f;
    place(m_testNoteBtn, body.x + kPad,                       footerY, btnW, kRowH);
    place(m_cancelBtn,   body.x + body.w - kPad - btnW * 2 - 4, footerY, btnW, kRowH);
    place(m_captureBtn,  body.x + body.w - kPad - btnW,         footerY, btnW, kRowH);
}

void FwAutoSampleDialog::layoutRunningWidgets(const Rect& body, UIContext& ctx) {
    const float pbY = body.y + 80.0f;
    const float pbW = body.w - kPad * 2;
    m_progressBar.layout({body.x + kPad, pbY, pbW, 20.0f}, ctx);

    const float footerY = body.y + body.h - kFooterH + 6;
    const float btnW = 96.0f;
    m_stopBtn.layout({body.x + body.w - kPad - btnW, footerY, btnW, kRowH}, ctx);
}

// ───────────────────────────────────────────────────────────────────
// Paint
// ───────────────────────────────────────────────────────────────────

void FwAutoSampleDialog::paintBody(UIContext& ctx) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    m_body = bodyRect(ctx);

    // Scrim
    r.drawRect(ctx.viewport.x, ctx.viewport.y,
               ctx.viewport.w, ctx.viewport.h,
               Color{0, 0, 0, 140});

    // Body
    r.drawRect(m_body.x, m_body.y, m_body.w, m_body.h,
               Color{30, 30, 35, 255});
    r.drawRect(m_body.x, m_body.y, m_body.w, kTitleBarH,
               Color{40, 40, 48, 255});

    const float titleSize = theme().metrics.fontSize;
    const char* titleText = m_running ? "Auto-Sampling…" : "Auto-Sample";
    tm.drawText(r, titleText, m_body.x + kPad, m_body.y + 8, titleSize,
                Color{220, 220, 220, 255});

    if (m_running) {
        layoutRunningWidgets(m_body, ctx);
        paintRunningMode(ctx, m_body);
    } else {
        layoutConfigWidgets(m_body, ctx);
        paintConfigMode(ctx, m_body);
    }
}

void FwAutoSampleDialog::paintConfigMode(UIContext& ctx, const Rect& body) {
    auto& r = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const float labelSize = theme().metrics.fontSizeSmall;
    const Color labelCol{180, 180, 185, 255};

    // Field labels — drawn at the y-position of each widget's layout slot.
    auto labelAt = [&](const char* text, const Rect& widget) {
        tm.drawText(r, text,
                    body.x + kPad,
                    widget.y + 6,
                    labelSize, labelCol);
    };
    labelAt("Capture name",   m_nameInput.bounds());
    labelAt("MIDI port",      m_portDD.bounds());
    labelAt("Audio input",    m_inputDD.bounds());
    labelAt("Level / VU",     m_levelKnob.bounds());
    labelAt("Note range",     m_lowNoteInput.bounds());
    labelAt("Step / layers",  m_stepDD.bounds());
    labelAt("Note / release", m_noteLengthInput.bounds());

    // Advanced section labels.
    if (m_advancedExpanded) {
        labelAt("Silence / pre-roll", m_silenceThreshInput.bounds());
    }

    // Render every widget.
    m_nameInput.render(ctx);
    m_portDD.render(ctx);
    m_channelDD.render(ctx);
    m_inputDD.render(ctx);
    m_channelsDD.render(ctx);
    m_levelKnob.render(ctx);
    m_inputMeter.render(ctx);
    m_lowNoteInput.render(ctx);
    m_highNoteInput.render(ctx);
    m_stepDD.render(ctx);
    m_velLayersDD.render(ctx);
    m_noteLengthInput.render(ctx);
    m_releaseTailInput.render(ctx);
    m_replaceToggle.render(ctx);
    m_trimToggle.render(ctx);
    m_advancedBtn.render(ctx);
    if (m_advancedExpanded) {
        m_silenceThreshInput.render(ctx);
        m_preRollInput.render(ctx);
    }
    m_testNoteBtn.render(ctx);
    m_cancelBtn.render(ctx);
    m_captureBtn.render(ctx);

    // Total / estimate line, just above the footer.
    const float estimateY = body.y + body.h - kFooterH - 14;
    char est[96];
    const int total = totalSampleCount();
    const int secs  = static_cast<int>(std::round(estimatedSecondsTotal()));
    std::snprintf(est, sizeof(est),
                  "Total: %d samples  ·  est. %d:%02d",
                  total, secs / 60, secs % 60);
    tm.drawText(r, est, body.x + kPad, estimateY, labelSize,
                Color{160, 160, 165, 255});
}

void FwAutoSampleDialog::paintRunningMode(UIContext& ctx, const Rect& body) {
    auto& r = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const float fs = theme().metrics.fontSize;
    const float fsSmall = theme().metrics.fontSizeSmall;

    char headline[128];
    if (m_worker) {
        const auto& s = m_worker->status();
        std::snprintf(headline, sizeof(headline),
                      "Capturing %s…   %d / %d",
                      m_cfg.captureName.c_str(),
                      s.completedSamples, s.totalSamples);
    } else {
        std::snprintf(headline, sizeof(headline), "Capturing %s…",
                      m_cfg.captureName.c_str());
    }
    tm.drawText(r, headline, body.x + kPad, body.y + kTitleBarH + 8, fs,
                Color{220, 220, 220, 255});

    m_progressBar.render(ctx);

    if (m_worker) {
        const auto& s = m_worker->status();
        if (s.currentNote >= 0) {
            char now[64];
            std::snprintf(now, sizeof(now), "Now: %s vel %d",
                          ::yawn::util::midiNoteName(s.currentNote).c_str(),
                          s.currentVel);
            tm.drawText(r, now, body.x + kPad,
                        m_progressBar.bounds().y + m_progressBar.bounds().h + 8,
                        fsSmall, Color{180, 180, 185, 255});
        }
    }

    m_stopBtn.render(ctx);
}

// ───────────────────────────────────────────────────────────────────
// Event dispatch
// ───────────────────────────────────────────────────────────────────

Widget* FwAutoSampleDialog::findWidgetAt(float sx, float sy) {
    auto check = [&](Widget* w) -> Widget* {
        if (!w) return nullptr;
        const Rect& b = w->bounds();
        if (sx < b.x || sx >= b.x + b.w) return nullptr;
        if (sy < b.y || sy >= b.y + b.h) return nullptr;
        return w;
    };
    if (m_running) {
        if (auto* w = check(&m_stopBtn)) return w;
        return nullptr;
    }
    Widget* candidates[] = {
        &m_nameInput, &m_portDD, &m_channelDD, &m_inputDD, &m_channelsDD,
        &m_levelKnob,                              // VU meter is display-only
        &m_lowNoteInput, &m_highNoteInput, &m_stepDD, &m_velLayersDD,
        &m_noteLengthInput, &m_releaseTailInput,
        &m_replaceToggle, &m_trimToggle,
        &m_advancedBtn,
        &m_silenceThreshInput, &m_preRollInput,   // ignored when collapsed
        &m_testNoteBtn, &m_cancelBtn, &m_captureBtn,
    };
    for (Widget* w : candidates) {
        // Skip hidden advanced fields when collapsed.
        if (!m_advancedExpanded &&
            (w == &m_silenceThreshInput || w == &m_preRollInput))
            continue;
        if (auto* hit = check(w)) return hit;
    }
    return nullptr;
}

bool FwAutoSampleDialog::onMouseDown(MouseEvent& e) {
    if (auto* w = findWidgetAt(e.x, e.y)) {
        return w->dispatchMouseDown(e);
    }
    // Block all clicks within the body so they don't reach the scrim.
    return e.x >= m_body.x && e.x < m_body.x + m_body.w &&
           e.y >= m_body.y && e.y < m_body.y + m_body.h;
}

bool FwAutoSampleDialog::onMouseUp(MouseEvent& e) {
    // Forward to whichever widget is currently captured (if any).
    if (auto* cap = Widget::capturedWidget()) {
        cap->dispatchMouseUp(e);
        return true;
    }
    if (auto* w = findWidgetAt(e.x, e.y)) {
        return w->dispatchMouseUp(e);
    }
    return false;
}

bool FwAutoSampleDialog::onMouseMove(MouseMoveEvent& e) {
    if (auto* cap = Widget::capturedWidget()) {
        cap->dispatchMouseMove(e);
        return true;
    }
    if (auto* w = findWidgetAt(e.x, e.y)) {
        w->dispatchMouseMove(e);
    }
    return false;
}

bool FwAutoSampleDialog::onKey(KeyEvent& e) {
    if (m_running) {
        if (e.key == Key::Escape) {
            onStopClicked();
            return true;
        }
        return false;
    }

    // If the capture-name field is editing, route editing keys
    // (Backspace / Delete / arrows / Home / End) to it first. Enter
    // commits the edit + triggers Capture in one go; Escape ends the
    // edit cleanly (FwTextInput::onKeyDown handles that internally).
    if (m_nameInput.isEditing()) {
        switch (e.key) {
            case Key::Escape:
                m_nameInput.dispatchKeyDown(e);   // ends edit
                return true;
            case Key::Enter:
                m_nameInput.dispatchKeyDown(e);   // commits edit
                return true;
            case Key::Backspace:
            case Key::Delete:
            case Key::Left:
            case Key::Right:
            case Key::Home:
            case Key::End:
                m_nameInput.dispatchKeyDown(e);
                return true;
            default:
                break;
        }
    }

    if (e.key == Key::Escape) {
        if (m_onResult) m_onResult(Result::Cancelled);
        close();
        return true;
    }
    if (e.key == Key::Enter) {
        onCaptureClicked();
        return true;
    }
    return false;
}

void FwAutoSampleDialog::onDismiss() {
    sendOffIfHeld(m_ctx.midi, m_testNoteHeld,
                   m_testNotePort, m_testNoteChannel);
    if (m_running && m_worker) {
        m_worker->abort();
    }
}

// ───────────────────────────────────────────────────────────────────
// Action handlers
// ───────────────────────────────────────────────────────────────────

void FwAutoSampleDialog::onCaptureClicked() {
    if (!m_ctx.engine || !m_ctx.midi || !m_ctx.target) {
        LOG_ERROR("AutoSample", "Capture clicked with incomplete context");
        return;
    }
    readConfigFromWidgets();
    if (m_cfg.captureName.empty()) {
        LOG_WARN("AutoSample", "Empty capture name — using fallback");
        m_cfg.captureName = "capture";
        m_cfg.captureFolder = m_ctx.samplesRoot / m_cfg.captureName;
    }

    auto startWorker = [this]() {
        m_worker = std::make_unique<audio::AutoSampleWorker>(*m_ctx.engine,
                                                              *m_ctx.midi);
        m_worker->setOnFinished([this](audio::AutoSampleStatus::Phase p) {
            onWorkerFinished(p);
        });
        if (!m_worker->start(m_cfg, m_ctx.target)) {
            const auto& s = m_worker->status();
            LOG_ERROR("AutoSample", "Capture failed to start: %s",
                      s.errorMessage.c_str());
            if (m_onResult) m_onResult(Result::Error);
            close();
            return;
        }
        m_running = true;
        m_progressBar.setDeterminate();
        m_progressBar.setValue(0.0f);
    };

    // Folder collision check — if the target subfolder already exists,
    // prompt before nuking it.
    std::error_code ec;
    if (fs::exists(m_cfg.captureFolder, ec)) {
        std::string msg = "A folder named '" + m_cfg.captureName +
                          "' already exists in the project's samples "
                          "directory.\n\nIts contents will be replaced "
                          "with the new capture.";
        ConfirmDialog::promptCustom(
            "Folder Exists", std::move(msg),
            "Overwrite", "Cancel",
            startWorker,
            /*onCancel*/ {});
    } else {
        startWorker();
    }
}

void FwAutoSampleDialog::onTestNoteClicked() {
    if (!m_ctx.midi) return;
    readConfigFromWidgets();   // pull latest port + channel from form

    if (m_cfg.midiOutputPortIndex < 0 ||
        m_cfg.midiOutputPortIndex >= m_ctx.midi->openOutputPortCount()) {
        LOG_WARN("AutoSample", "Test note: no valid MIDI output port selected");
        return;
    }

    // Toggle: first click sends Note-On (sustained), second click
    // sends Note-Off. Lets the user hold a note as long as needed to
    // dial in the recording-level knob against the live VU meter.
    midi::MidiBuffer buf;
    midi::MidiMessage msg;
    msg.channel = static_cast<uint8_t>(m_cfg.midiChannel);
    msg.note = 60;        // C4
    msg.frameOffset = 0;

    if (!m_testNoteHeld) {
        msg.type     = midi::MidiMessage::Type::NoteOn;
        msg.velocity = 100 * 257;     // YAWN's 16-bit internal velocity
        buf.addMessage(msg);
        m_ctx.midi->sendToOutput(m_cfg.midiOutputPortIndex, buf);
        m_testNoteHeld    = true;
        m_testNotePort    = m_cfg.midiOutputPortIndex;
        m_testNoteChannel = m_cfg.midiChannel;
        m_testNoteBtn.setLabel("Stop Note");
        LOG_INFO("AutoSample",
                 "Test note ON  C4 → port %d ch %d (toggle)",
                 m_cfg.midiOutputPortIndex, m_cfg.midiChannel + 1);
    } else {
        msg.type     = midi::MidiMessage::Type::NoteOff;
        msg.velocity = 0;
        buf.addMessage(msg);
        m_ctx.midi->sendToOutput(m_testNotePort, buf);
        m_testNoteHeld = false;
        m_testNoteBtn.setLabel("Test Note");
        LOG_INFO("AutoSample", "Test note OFF C4 (toggle)");
    }
}


void FwAutoSampleDialog::onStopClicked() {
    if (m_worker) m_worker->abort();
    // onWorkerFinished will fire and close us via Aborted.
}

void FwAutoSampleDialog::onWorkerFinished(audio::AutoSampleStatus::Phase phase) {
    using Phase = audio::AutoSampleStatus::Phase;
    sendOffIfHeld(m_ctx.midi, m_testNoteHeld,
                   m_testNotePort, m_testNoteChannel);
    Result r = Result::Done;
    if (phase == Phase::Aborted)      r = Result::Cancelled;
    else if (phase == Phase::Error)   r = Result::Error;
    else                                r = Result::Done;
    if (m_onResult) m_onResult(r);
    m_running = false;
    close();
}

} // namespace fw2
} // namespace ui
} // namespace yawn
