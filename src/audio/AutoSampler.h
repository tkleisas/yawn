#pragma once
//
// Auto-Sampler — captures samples from an external MIDI source by
// playing each (note, velocity) pair across a configurable grid,
// recording the audio return into a per-capture subfolder, and
// populating a Multisampler instrument's zones from the result.
//
// Wiring:
//   * UI thread owns an AutoSampler instance + the modal dialog
//   * Dialog collects parameters → fills AutoSampleConfig
//   * On confirm, AutoSampler::start(...) kicks off the worker state
//     machine; tick() is called once per UI frame from App::update
//   * Each tick advances state by elapsed wall-clock time:
//       send Note-On → wait noteLengthSec → send Note-Off →
//       wait releaseTailSec → save WAV → next pair
//   * On done, writes <captureFolder>/manifest.json and adds zones
//     to the target Multisampler. Dialog closes.
//
// Audio capture is via AudioEngine's PrivateCaptureRequest side
// channel — independent of transport / clip / track recording. MIDI
// output is via MidiEngine::sendToOutput(portIndex, buf).
//
// Capture folder: <project>.yawn/samples/<sanitized_capture_name>/
// Per-sample filename: <rootName>_v<vel>.wav (e.g. "C2_v100.wav")
//
// Lifetime: AutoSampler is a value-typed App member. The dialog and
// worker live inside it.

#include "audio/AudioEngine.h"
#include "instruments/Multisampler.h"
#include "util/FileIO.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn { namespace midi { class MidiEngine; } }

namespace yawn {
namespace audio {

// All the parameters the dialog collects.
struct AutoSampleConfig {
    std::string captureName        = "capture";   // sanitized to filename-safe
    std::filesystem::path captureFolder;          // .../samples/<captureName>/
    int     midiOutputPortIndex    = 0;            // index into MidiEngine open outputs
    int     midiChannel            = 0;            // 0..15
    int     audioInputChannel      = 0;            // 0-based hardware input channel
    int     audioChannels          = 2;            // 1=mono, 2=stereo
    int     lowNote                = 36;           // MIDI 0..127 (C2)
    int     highNote               = 96;           // (C7)
    int     noteStep               = 4;            // every N semitones (default = major third)
    // Default 4 velocity layers — industry standard (Logic Sampler Auto,
    // SampleRobot, Redmatica AutoSampler all default here). Captures
    // pp / mp / mf / ff dynamics without making the run unreasonably
    // long. velocityLayerPreset(4) returns evenly-spaced [16..127].
    std::vector<int> velocityLayers = {44, 72, 99, 127};
    float   noteLengthSec          = 2.0f;         // how long the note is held
    float   releaseTailSec         = 1.5f;         // post-Note-Off recording tail
    float   preRollSec             = 0.3f;         // silence before each note (lets prev release fade)
    util::BitDepth bitDepth        = util::BitDepth::Float32;
    bool    replaceExistingZones   = true;
    bool    trimLeadingSilence     = true;
    float   silenceThresholdDb     = -60.0f;
    // Software gain applied to captured samples before save / trim
    // analysis. 0 dB = pass-through. Lets the user trim a too-hot
    // synth or boost a quiet preset without retracking — the VU meter
    // in the dialog reflects pre-gain input level so the knob's
    // "right" setting is whatever lands the loudest preset just below
    // 0 dBFS in the meter once gain is applied (UI multiplies the
    // displayed peak by the same gain for that visual feedback loop).
    float   recordingLevelDb       = 0.0f;
};

// Public state of the running worker — polled by the dialog for the
// progress bar / status line / cancel button.
struct AutoSampleStatus {
    enum class Phase {
        Idle,
        PreRoll,           // waiting before next Note-On
        Capturing,         // Note-On sent, capturing first noteLengthSec
        ReleaseTail,       // Note-Off sent, capturing tail
        SavingWav,         // writing .wav to disk
        Done,
        Error,
        Aborted,
    };
    Phase phase = Phase::Idle;
    int   currentNote = -1;       // MIDI note being captured (-1 = none)
    int   currentVel  = 0;
    int   completedSamples = 0;   // count of samples captured so far
    int   totalSamples     = 0;   // total grid size
    std::string errorMessage;     // populated when phase == Error
};

// The state-machine worker. Owned by AutoSampler. tick() is the only
// per-frame entry point; everything else is set-up and tear-down.
class AutoSampleWorker {
public:
    AutoSampleWorker(audio::AudioEngine& engine, midi::MidiEngine& midi)
        : m_engine(engine), m_midi(midi) {}
    ~AutoSampleWorker() { abort(); }

    AutoSampleWorker(const AutoSampleWorker&) = delete;
    AutoSampleWorker& operator=(const AutoSampleWorker&) = delete;

    // Begin a capture run with the given config + target Multisampler.
    // The Multisampler must outlive the worker. Returns false on
    // setup failure (folder creation, no notes in grid, etc.) and
    // populates m_status.errorMessage.
    bool start(const AutoSampleConfig& cfg,
               instruments::Multisampler* target);

    // Per-frame tick — drives the state machine. Cheap when idle.
    void tick();

    // User-initiated cancel. Sends a final Note-Off, aborts any
    // in-flight private capture, marks status as Aborted. Subsequent
    // ticks are no-ops.
    void abort();

    bool isActive() const { return m_status.phase != AutoSampleStatus::Phase::Idle &&
                                   m_status.phase != AutoSampleStatus::Phase::Done &&
                                   m_status.phase != AutoSampleStatus::Phase::Error &&
                                   m_status.phase != AutoSampleStatus::Phase::Aborted; }
    const AutoSampleStatus& status() const { return m_status; }
    const AutoSampleConfig& config() const { return m_cfg; }

    // Result hook: fired exactly once when the worker reaches a
    // terminal state (Done / Error / Aborted). Useful for closing
    // the dialog.
    void setOnFinished(std::function<void(AutoSampleStatus::Phase)> cb) {
        m_onFinished = std::move(cb);
    }

private:
    void enterPhase(AutoSampleStatus::Phase p);
    void advanceToNextSample();   // bumps currentNote / currentVel / completedSamples
    void sendNoteOn(int note, int vel);
    void sendNoteOff(int note);
    void startCaptureRequest(double seconds);   // arms private capture
    void saveCurrentSample();                    // writes the captured buffer to .wav
    void finalizeAndAddZones();                  // builds Multisampler zones + manifest
    bool writeManifest();                        // writes <folder>/manifest.json

    // Helpers
    int  framesForSeconds(double s) const;
    std::string sampleFilename(int note, int vel) const;

    audio::AudioEngine&                 m_engine;
    midi::MidiEngine&                   m_midi;
    instruments::Multisampler*          m_target = nullptr;

    AutoSampleConfig                    m_cfg;
    AutoSampleStatus                    m_status;

    // Grid iteration: list of (note, velocity) pairs to capture, in
    // order. Built up-front from cfg so totalSamples is known.
    struct Pair { int note; int vel; };
    std::vector<Pair>                   m_grid;
    int                                  m_gridIdx = 0;

    // Per-sample state.
    std::unique_ptr<audio::AudioEngine::PrivateCaptureRequest> m_request;
    std::chrono::steady_clock::time_point                       m_phaseStart;
    bool                                                        m_noteHeld = false;

    // List of (note, vel, captured-frames, file-path) to populate
    // zones at the end. Filled as each sample saves.
    struct CapturedSample {
        int   note;
        int   vel;
        int   frames;
        int   channels;
        std::filesystem::path path;
        std::vector<float>    buffer;   // moved out of request
    };
    std::vector<CapturedSample>         m_captured;

    std::function<void(AutoSampleStatus::Phase)> m_onFinished;
};

// Convenience: sanitize a string into a filesystem-safe basename.
// Lowercases, replaces whitespace + illegal chars with `_`, collapses
// runs of `_`, trims leading/trailing punctuation.
std::string sanitizeCaptureName(const std::string& input);

// Build the default capture name from a track name (or "capture" if
// the track name is empty / unrecognisable after sanitization).
std::string defaultCaptureName(const std::string& trackName);

// Velocity-layer presets. layerCount is one of {1,2,3,4,8}; returns
// evenly-spaced velocities ascending from `minVel` to 127.
//   1 layer → [120]
//   2 layers → [60, 120]
//   3 layers → [40, 80, 120]
//   4 layers → [30, 60, 90, 120]
//   8 layers → [16, 32, ..., 127]
std::vector<int> velocityLayerPreset(int layerCount);

} // namespace audio
} // namespace yawn
