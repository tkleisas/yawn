#include "audio/AutoSampler.h"

#include "midi/MidiEngine.h"
#include "midi/MidiTypes.h"
#include "util/Logger.h"
#include "util/NoteNames.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace yawn {
namespace audio {

// ─── Standalone helpers ──────────────────────────────────────────────

std::string sanitizeCaptureName(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    auto isSafe = [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.';
    };
    bool prevUnderscore = false;
    for (unsigned char c : input) {
        unsigned char lc = static_cast<unsigned char>(std::tolower(c));
        if (isSafe(lc)) {
            out.push_back(static_cast<char>(lc));
            prevUnderscore = false;
        } else if (!prevUnderscore && !out.empty()) {
            out.push_back('_');
            prevUnderscore = true;
        }
    }
    // Trim trailing underscores / dots
    while (!out.empty() && (out.back() == '_' || out.back() == '.'))
        out.pop_back();
    if (out.empty()) out = "capture";
    return out;
}

std::string defaultCaptureName(const std::string& trackName) {
    std::string base = sanitizeCaptureName(trackName);
    if (base.empty() || base == "capture") return "capture";
    return base + "_capture";
}

std::vector<int> velocityLayerPreset(int layerCount) {
    layerCount = std::max(1, std::min(8, layerCount));
    std::vector<int> out;
    out.reserve(layerCount);
    if (layerCount == 1) {
        out.push_back(120);
    } else {
        // Spread evenly across 16..127 so the lowest layer is still
        // audibly soft and the highest hits hard.
        for (int i = 1; i <= layerCount; ++i) {
            int v = static_cast<int>(std::round(
                16 + (127.0 - 16.0) * (static_cast<double>(i) / layerCount)));
            out.push_back(std::clamp(v, 1, 127));
        }
    }
    return out;
}

// ─── AutoSampleWorker ────────────────────────────────────────────────

bool AutoSampleWorker::start(const AutoSampleConfig& cfg,
                              instruments::Multisampler* target) {
    if (isActive()) {
        m_status.errorMessage = "Worker already running";
        m_status.phase = AutoSampleStatus::Phase::Error;
        return false;
    }
    m_cfg = cfg;
    m_target = target;
    m_grid.clear();
    m_captured.clear();
    m_gridIdx = 0;
    m_noteHeld = false;
    m_request.reset();
    m_status = AutoSampleStatus{};

    if (!m_target) {
        m_status.errorMessage = "No Multisampler target";
        m_status.phase = AutoSampleStatus::Phase::Error;
        return false;
    }
    if (m_cfg.lowNote > m_cfg.highNote) std::swap(m_cfg.lowNote, m_cfg.highNote);
    if (m_cfg.noteStep < 1) m_cfg.noteStep = 1;
    if (m_cfg.velocityLayers.empty()) m_cfg.velocityLayers = {100};

    // Build the (note, vel) grid up front so we know totalSamples
    // for the progress display.
    for (int n = m_cfg.lowNote; n <= m_cfg.highNote; n += m_cfg.noteStep) {
        for (int v : m_cfg.velocityLayers) {
            m_grid.push_back({n, std::clamp(v, 1, 127)});
        }
    }
    if (m_grid.empty()) {
        m_status.errorMessage = "Empty grid (check note range / step)";
        m_status.phase = AutoSampleStatus::Phase::Error;
        return false;
    }

    // Validate MIDI port + audio input via the engines.
    if (m_cfg.midiOutputPortIndex < 0 ||
        m_cfg.midiOutputPortIndex >= m_midi.openOutputPortCount()) {
        m_status.errorMessage = "Invalid MIDI output port";
        m_status.phase = AutoSampleStatus::Phase::Error;
        return false;
    }

    // Create the capture folder. Caller is responsible for collision-
    // checking BEFORE calling start (the dialog handles that via a
    // confirm dialog). Here we just create the folder if missing and
    // proceed (overwriting any existing contents).
    std::error_code ec;
    fs::create_directories(m_cfg.captureFolder, ec);
    if (ec) {
        m_status.errorMessage = "Cannot create capture folder: " + ec.message();
        m_status.phase = AutoSampleStatus::Phase::Error;
        return false;
    }

    m_status.totalSamples = static_cast<int>(m_grid.size());
    m_status.completedSamples = 0;
    m_status.currentNote = -1;
    m_status.currentVel = 0;

    LOG_INFO("AutoSample",
             "Starting capture '%s': %d samples, port=%d ch=%d, range=%d-%d step=%d, "
             "noteLen=%.2fs releaseTail=%.2fs",
             m_cfg.captureName.c_str(), m_status.totalSamples,
             m_cfg.midiOutputPortIndex, m_cfg.midiChannel,
             m_cfg.lowNote, m_cfg.highNote, m_cfg.noteStep,
             m_cfg.noteLengthSec, m_cfg.releaseTailSec);

    // Kick off the first pre-roll.
    enterPhase(AutoSampleStatus::Phase::PreRoll);
    return true;
}

void AutoSampleWorker::abort() {
    if (m_status.phase == AutoSampleStatus::Phase::Idle ||
        m_status.phase == AutoSampleStatus::Phase::Done ||
        m_status.phase == AutoSampleStatus::Phase::Error ||
        m_status.phase == AutoSampleStatus::Phase::Aborted)
        return;
    if (m_noteHeld && m_gridIdx < static_cast<int>(m_grid.size())) {
        sendNoteOff(m_grid[m_gridIdx].note);
    }
    m_engine.abortPrivateCapture();
    m_request.reset();
    m_status.phase = AutoSampleStatus::Phase::Aborted;
    m_noteHeld = false;
    LOG_INFO("AutoSample", "Capture aborted");
    if (m_onFinished) m_onFinished(m_status.phase);
}

void AutoSampleWorker::tick() {
    using Phase = AutoSampleStatus::Phase;
    if (m_status.phase == Phase::Idle ||
        m_status.phase == Phase::Done ||
        m_status.phase == Phase::Error ||
        m_status.phase == Phase::Aborted)
        return;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - m_phaseStart).count();

    switch (m_status.phase) {
        case Phase::PreRoll: {
            if (elapsed < m_cfg.preRollSec) return;
            // Move to next pair (or finalize if grid exhausted).
            if (m_gridIdx >= static_cast<int>(m_grid.size())) {
                finalizeAndAddZones();
                return;
            }
            const auto& p = m_grid[m_gridIdx];
            m_status.currentNote = p.note;
            m_status.currentVel  = p.vel;
            // Arm the private capture for noteLength + releaseTail seconds
            // total — the audio thread will fill it across both phases.
            startCaptureRequest(m_cfg.noteLengthSec + m_cfg.releaseTailSec);
            sendNoteOn(p.note, p.vel);
            m_noteHeld = true;
            enterPhase(Phase::Capturing);
            return;
        }
        case Phase::Capturing: {
            if (elapsed < m_cfg.noteLengthSec) return;
            // Send Note-Off; private capture continues during release tail.
            sendNoteOff(m_grid[m_gridIdx].note);
            m_noteHeld = false;
            enterPhase(Phase::ReleaseTail);
            return;
        }
        case Phase::ReleaseTail: {
            if (!m_request) {
                // Shouldn't happen; defensive.
                m_status.phase = Phase::Error;
                m_status.errorMessage = "Capture request lost";
                if (m_onFinished) m_onFinished(m_status.phase);
                return;
            }
            // Stay here until the private capture finishes (it ends when
            // framesWritten >= frames, which we sized for the full
            // noteLength + releaseTail).
            if (!m_request->done.load(std::memory_order_acquire)) return;
            // Capture done — save the sample.
            enterPhase(Phase::SavingWav);
            saveCurrentSample();
            // Increment progress, advance to the next pair.
            ++m_status.completedSamples;
            ++m_gridIdx;
            m_request.reset();
            enterPhase(Phase::PreRoll);
            return;
        }
        default:
            return;
    }
}

void AutoSampleWorker::enterPhase(AutoSampleStatus::Phase p) {
    m_status.phase = p;
    m_phaseStart = std::chrono::steady_clock::now();
}

int AutoSampleWorker::framesForSeconds(double s) const {
    return static_cast<int>(std::ceil(s * m_engine.sampleRate()));
}

std::string AutoSampleWorker::sampleFilename(int note, int vel) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s_v%d.wav",
                  ::yawn::util::midiNoteName(note).c_str(), vel);
    return buf;
}

void AutoSampleWorker::sendNoteOn(int note, int vel) {
    midi::MidiBuffer buf;
    midi::MidiMessage msg;
    msg.type    = midi::MidiMessage::Type::NoteOn;
    msg.channel = static_cast<uint8_t>(std::clamp(m_cfg.midiChannel, 0, 15));
    msg.note    = static_cast<uint8_t>(std::clamp(note, 0, 127));
    // velocity in MIDI 1.0 → scale 7-bit to YAWN's 16-bit internal.
    msg.velocity = static_cast<uint16_t>(std::clamp(vel, 1, 127) * 257);
    msg.frameOffset = 0;
    buf.addMessage(msg);
    m_midi.sendToOutput(m_cfg.midiOutputPortIndex, buf);
}

void AutoSampleWorker::sendNoteOff(int note) {
    midi::MidiBuffer buf;
    midi::MidiMessage msg;
    msg.type    = midi::MidiMessage::Type::NoteOff;
    msg.channel = static_cast<uint8_t>(std::clamp(m_cfg.midiChannel, 0, 15));
    msg.note    = static_cast<uint8_t>(std::clamp(note, 0, 127));
    msg.velocity = 0;
    msg.frameOffset = 0;
    buf.addMessage(msg);
    m_midi.sendToOutput(m_cfg.midiOutputPortIndex, buf);
}

void AutoSampleWorker::startCaptureRequest(double seconds) {
    m_request = std::make_unique<audio::AudioEngine::PrivateCaptureRequest>();
    m_request->inputChannel = m_cfg.audioInputChannel;
    m_request->channels = std::clamp(m_cfg.audioChannels, 1, 2);
    m_request->frames = framesForSeconds(seconds);
    m_request->buffer.resize(static_cast<size_t>(m_request->frames) *
                              m_request->channels, 0.0f);
    if (!m_engine.startPrivateCapture(m_request.get())) {
        // Highly unlikely — only one capture in flight at a time.
        // Surface as error.
        m_status.phase = AutoSampleStatus::Phase::Error;
        m_status.errorMessage = "Audio engine private capture busy";
        if (m_onFinished) m_onFinished(m_status.phase);
    }
}

void AutoSampleWorker::saveCurrentSample() {
    if (!m_request) return;
    if (m_gridIdx >= static_cast<int>(m_grid.size())) return;

    const Pair& p = m_grid[m_gridIdx];
    const int channels = m_request->channels;
    const int64_t framesWritten = m_request->framesWritten.load();
    if (framesWritten <= 0) {
        LOG_WARN("AutoSample", "Empty capture for note %d vel %d (no audio?)",
                 p.note, p.vel);
        // Still record it — empty zone is better than silently skipping.
    }

    // Apply the user-set recording level (in-place, before trim
    // analysis, so the silence threshold sees the boosted signal).
    // Skip the multiplication when gain is unity to avoid touching
    // the buffer in the common case.
    if (m_cfg.recordingLevelDb != 0.0f && framesWritten > 0) {
        const float gain = std::pow(10.0f, m_cfg.recordingLevelDb / 20.0f);
        const int64_t total = framesWritten * channels;
        for (int64_t i = 0; i < total; ++i) {
            m_request->buffer[i] *= gain;
        }
    }

    // Optional leading-silence trim. Walks frame-by-frame from the start
    // until peak across channels exceeds the threshold; truncates the
    // buffer to the post-trim region. Always leaves at least a few
    // milliseconds of head so attacks aren't clipped.
    int trimStart = 0;
    if (m_cfg.trimLeadingSilence && framesWritten > 0) {
        const float thresh =
            std::pow(10.0f, m_cfg.silenceThresholdDb / 20.0f);
        for (int64_t f = 0; f < framesWritten; ++f) {
            float peak = 0.0f;
            for (int c = 0; c < channels; ++c) {
                peak = std::max(peak,
                    std::abs(m_request->buffer[f * channels + c]));
            }
            if (peak > thresh) {
                // Back up ~10 ms so we don't shave the attack.
                const int margin = framesForSeconds(0.010);
                trimStart = std::max(0, static_cast<int>(f) - margin);
                break;
            }
        }
    }

    const int64_t finalFrames = std::max<int64_t>(0, framesWritten - trimStart);

    fs::path filePath = m_cfg.captureFolder / sampleFilename(p.note, p.vel);
    const float* dataStart =
        m_request->buffer.data() + trimStart * channels;
    if (!util::saveAudioFile(filePath.string(),
                              dataStart,
                              static_cast<int>(finalFrames),
                              channels,
                              static_cast<int>(m_engine.sampleRate()),
                              util::ExportFormat::WAV,
                              m_cfg.bitDepth)) {
        LOG_WARN("AutoSample", "Failed to save %s", filePath.string().c_str());
        // Continue — the user gets a partial capture rather than a hard
        // abort. Missing zones simply won't appear in the manifest.
        return;
    }

    CapturedSample cs;
    cs.note     = p.note;
    cs.vel      = p.vel;
    cs.frames   = static_cast<int>(finalFrames);
    cs.channels = channels;
    cs.path     = filePath;
    // Move the trimmed buffer into the captured-sample record so we
    // don't have to reread from disk when adding the zone.
    cs.buffer.assign(dataStart, dataStart + finalFrames * channels);
    m_captured.push_back(std::move(cs));
}

void AutoSampleWorker::finalizeAndAddZones() {
    if (!m_target) {
        m_status.phase = AutoSampleStatus::Phase::Error;
        m_status.errorMessage = "Multisampler target gone";
        if (m_onFinished) m_onFinished(m_status.phase);
        return;
    }

    if (m_cfg.replaceExistingZones) {
        m_target->clearZones();
    }

    // Group captured samples by note. For each group, compute key
    // ranges that interpolate between the captured roots; for each
    // velocity, slice [low, high] into bands so they don't overlap.
    // We keep it simple: per-note key range = [noteStep/2 below root,
    // noteStep/2 above root - 1], full velocity layer span.

    // Build a sorted, unique list of roots actually captured.
    std::vector<int> roots;
    for (const auto& cs : m_captured)
        if (std::find(roots.begin(), roots.end(), cs.note) == roots.end())
            roots.push_back(cs.note);
    std::sort(roots.begin(), roots.end());

    auto keyRangeForRoot = [&](int root) -> std::pair<int,int> {
        // Find neighbouring roots in `roots` to set the boundary.
        // Below: midpoint to previous root (or 0 if none).
        // Above: midpoint to next root (or 127 if none).
        auto it = std::find(roots.begin(), roots.end(), root);
        int below = 0, above = 127;
        if (it != roots.begin()) {
            int prev = *(it - 1);
            below = (prev + root) / 2 + 1;
        } else {
            below = std::max(0, root - m_cfg.noteStep);
        }
        if (it + 1 != roots.end()) {
            int next = *(it + 1);
            above = (root + next) / 2;
        } else {
            above = std::min(127, root + m_cfg.noteStep - 1);
        }
        return {below, above};
    };

    // Sort velocity layers ascending so we can slice [low, high] bands.
    std::vector<int> sortedVels = m_cfg.velocityLayers;
    std::sort(sortedVels.begin(), sortedVels.end());
    auto velRangeForLayer = [&](int vel) -> std::pair<int,int> {
        auto it = std::find(sortedVels.begin(), sortedVels.end(), vel);
        int low = 1, high = 127;
        if (it != sortedVels.begin()) {
            int prev = *(it - 1);
            low = (prev + vel) / 2 + 1;
        }
        if (it + 1 != sortedVels.end()) {
            int next = *(it + 1);
            high = (vel + next) / 2;
        } else {
            high = 127;
        }
        return {std::clamp(low, 1, 127), std::clamp(high, 1, 127)};
    };

    for (const auto& cs : m_captured) {
        instruments::Multisampler::Zone z;
        z.sampleData     = cs.buffer;
        z.sampleFrames   = cs.frames;
        z.sampleChannels = cs.channels;
        // Stamp the engine's current rate at capture time. Without
        // this, a 48-kHz capture (e.g. WASAPI loopback override)
        // played by a 44.1-kHz engine would be ~147 cents flat.
        z.sampleRate     = static_cast<int>(m_engine.sampleRate());
        z.rootNote = cs.note;
        auto kr = keyRangeForRoot(cs.note);
        z.lowKey  = kr.first;
        z.highKey = kr.second;
        auto vr = velRangeForLayer(cs.vel);
        z.lowVel  = vr.first;
        z.highVel = vr.second;
        z.tune    = 0.0f;
        z.volume  = 1.0f;
        z.pan     = 0.0f;
        z.loop    = false;
        z.loopEnd = cs.frames;
        m_target->addZone(z);
    }

    if (!writeManifest()) {
        LOG_WARN("AutoSample", "Failed to write manifest.json — capture itself succeeded");
    }

    m_status.phase = AutoSampleStatus::Phase::Done;
    LOG_INFO("AutoSample",
             "Capture '%s' complete: %zu zones, folder=%s",
             m_cfg.captureName.c_str(), m_captured.size(),
             m_cfg.captureFolder.string().c_str());
    if (m_onFinished) m_onFinished(m_status.phase);
}

bool AutoSampleWorker::writeManifest() {
    json m;
    m["captureName"]      = m_cfg.captureName;
    m["sampleCount"]      = static_cast<int>(m_captured.size());
    m["midiPortName"]     = m_midi.openOutputPortName(m_cfg.midiOutputPortIndex);
    m["midiPortIndex"]    = m_cfg.midiOutputPortIndex;
    m["midiChannel"]      = m_cfg.midiChannel;
    m["audioInputChannel"]= m_cfg.audioInputChannel;
    m["audioChannels"]    = m_cfg.audioChannels;
    m["sampleRate"]       = static_cast<int>(m_engine.sampleRate());
    m["noteRange"]        = json::array({m_cfg.lowNote, m_cfg.highNote});
    m["noteStep"]         = m_cfg.noteStep;
    m["velocityLayers"]   = m_cfg.velocityLayers;
    m["noteLengthSec"]    = m_cfg.noteLengthSec;
    m["releaseTailSec"]   = m_cfg.releaseTailSec;
    m["preRollSec"]       = m_cfg.preRollSec;
    m["trimLeadingSilence"] = m_cfg.trimLeadingSilence;
    m["silenceThresholdDb"] = m_cfg.silenceThresholdDb;

    // Capture date in ISO 8601 — std::time + gmtime is enough.
    {
        std::time_t now = std::time(nullptr);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &now);
#else
        gmtime_r(&now, &tm_utc);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        m["captureDate"] = buf;
    }

    json samples = json::array();
    for (const auto& cs : m_captured) {
        json s;
        s["note"]     = cs.note;
        s["noteName"] = ::yawn::util::midiNoteName(cs.note);
        s["velocity"] = cs.vel;
        s["frames"]   = cs.frames;
        s["channels"] = cs.channels;
        s["file"]     = cs.path.filename().string();
        samples.push_back(s);
    }
    m["samples"] = samples;

    fs::path manifestPath = m_cfg.captureFolder / "manifest.json";
    std::ofstream f(manifestPath);
    if (!f) return false;
    f << m.dump(2);
    return f.good();
}

} // namespace audio
} // namespace yawn
