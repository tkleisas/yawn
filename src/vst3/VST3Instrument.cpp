#ifdef YAWN_HAS_VST3

#include "vst3/VST3Instrument.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>

namespace yawn {
namespace vst3 {

const instruments::InstrumentParameterInfo VST3Instrument::s_emptyParam = {};

VST3Instrument::VST3Instrument(const std::string& modulePath,
                                 const std::string& classIDString)
    : m_modulePath(modulePath)
    , m_classIDString(classIDString)
    , m_pluginID("vst3:" + classIDString)
{
}

void VST3Instrument::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    // Pre-allocate non-interleaved output buffers
    m_outLeft.resize(maxBlockSize, 0.0f);
    m_outRight.resize(maxBlockSize, 0.0f);

    // Load and instantiate the plugin
    VST3ModuleHandle handle;
    std::string error;
    if (!handle.load(m_modulePath, error)) {
        m_displayName = "[Failed: " + m_modulePath + "]";
        return;
    }

    m_instance = VST3PluginInstance::create(
        handle.module(), m_classIDString, sampleRate, maxBlockSize);

    if (!m_instance) {
        m_displayName = "[Failed to create VST3]";
        return;
    }

    m_displayName = m_instance->name();
    m_instance->setProcessing(true);

    buildParameterInfoCache();
}

void VST3Instrument::reset() {
    if (m_instance) {
        m_instance->setProcessing(false);
        m_instance->setActive(false);
        m_instance->setupProcessing(m_sampleRate, m_maxBlockSize);
        m_instance->setActive(true);
        m_instance->setProcessing(true);
    }
}

void VST3Instrument::process(float* buffer, int numFrames, int numChannels,
                               const midi::MidiBuffer& midi) {
    if (!m_instance || !m_instance->processor() || m_bypassed) return;

    // Convert YAWN MIDI to VST3 EventList
    Steinberg::Vst::EventList inputEvents;
    convertMidiToEventList(midi, inputEvents);

    // Clear output buffers
    std::memset(m_outLeft.data(), 0, numFrames * sizeof(float));
    std::memset(m_outRight.data(), 0, numFrames * sizeof(float));

    // Setup VST3 AudioBusBuffers
    float* outChannels[2] = { m_outLeft.data(), m_outRight.data() };

    Steinberg::Vst::AudioBusBuffers outputBus;
    outputBus.numChannels = 2;
    outputBus.silenceFlags = 0;
    outputBus.channelBuffers32 = outChannels;

    // Drain queued parameter changes for the processor
    Steinberg::Vst::ParameterChanges paramChanges;
    m_instance->drainParameterChanges(paramChanges);

    // Setup ProcessData
    Steinberg::Vst::ProcessData processData;
    processData.processMode = Steinberg::Vst::kRealtime;
    processData.symbolicSampleSize = Steinberg::Vst::kSample32;
    processData.numSamples = numFrames;
    processData.numInputs = 0;
    processData.inputs = nullptr;
    processData.numOutputs = 1;
    processData.outputs = &outputBus;
    processData.inputEvents = &inputEvents;
    processData.outputEvents = nullptr;
    processData.inputParameterChanges = paramChanges.getParameterCount() > 0 ? &paramChanges : nullptr;
    processData.outputParameterChanges = nullptr;
    processData.processContext = nullptr;

    // Process
    m_instance->processor()->process(processData);

    // ADD non-interleaved output to YAWN's interleaved buffer
    if (numChannels >= 2) {
        for (int i = 0; i < numFrames; ++i) {
            buffer[i * numChannels + 0] += m_outLeft[i];
            buffer[i * numChannels + 1] += m_outRight[i];
        }
    } else if (numChannels == 1) {
        for (int i = 0; i < numFrames; ++i) {
            buffer[i] += (m_outLeft[i] + m_outRight[i]) * 0.5f;
        }
    }
}

void VST3Instrument::convertMidiToEventList(const midi::MidiBuffer& midi,
                                              Steinberg::Vst::EventList& eventList) {
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        Steinberg::Vst::Event e = {};
        e.busIndex = 0;
        e.sampleOffset = msg.frameOffset;
        e.ppqPosition = 0;
        e.flags = Steinberg::Vst::Event::kIsLive;

        switch (msg.type) {
            case midi::MidiMessage::Type::NoteOn: {
                if (msg.velocity == 0) {
                    // NoteOn with velocity 0 is NoteOff
                    e.type = Steinberg::Vst::Event::kNoteOffEvent;
                    e.noteOff.channel = msg.channel;
                    e.noteOff.pitch = msg.note;
                    e.noteOff.velocity = 0.0f;
                    e.noteOff.noteId = -1;
                    e.noteOff.tuning = 0.0f;
                } else {
                    e.type = Steinberg::Vst::Event::kNoteOnEvent;
                    e.noteOn.channel = msg.channel;
                    e.noteOn.pitch = msg.note;
                    // Convert 16-bit velocity to 0-1 float
                    e.noteOn.velocity = static_cast<float>(msg.velocity) / 65535.0f;
                    e.noteOn.length = 0;
                    e.noteOn.noteId = -1;
                    e.noteOn.tuning = 0.0f;
                }
                eventList.addEvent(e);
                break;
            }
            case midi::MidiMessage::Type::NoteOff: {
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = msg.channel;
                e.noteOff.pitch = msg.note;
                e.noteOff.velocity = static_cast<float>(msg.velocity) / 65535.0f;
                e.noteOff.noteId = -1;
                e.noteOff.tuning = 0.0f;
                eventList.addEvent(e);
                break;
            }
            case midi::MidiMessage::Type::PolyPressure: {
                e.type = Steinberg::Vst::Event::kPolyPressureEvent;
                e.polyPressure.channel = msg.channel;
                e.polyPressure.pitch = msg.note;
                e.polyPressure.pressure = static_cast<float>(msg.value) / 4294967295.0f;
                e.polyPressure.noteId = -1;
                eventList.addEvent(e);
                break;
            }
            default:
                // CC, PitchBend, etc. are handled via IParameterChanges in VST3
                // (not via EventList). For now, skip them.
                break;
        }
    }
}

void VST3Instrument::buildParameterInfoCache() {
    m_paramInfos.clear();
    m_paramNames.clear();
    m_paramUnits.clear();
    m_paramIDs.clear();

    if (!m_instance) return;

    int count = m_instance->parameterCount();
    m_paramInfos.reserve(count);
    m_paramNames.reserve(count);
    m_paramUnits.reserve(count);
    m_paramIDs.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto vst3Info = m_instance->parameterInfo(i);

        // Skip hidden/read-only parameters
        if (vst3Info.flags & Steinberg::Vst::ParameterInfo::kIsHidden)
            continue;
        if (vst3Info.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly)
            continue;

        // Convert Steinberg char16 strings to std::string
        std::string paramName;
        for (int c = 0; vst3Info.title[c] != 0; ++c)
            paramName += static_cast<char>(vst3Info.title[c]);
        m_paramNames.push_back(paramName);

        std::string unitStr;
        for (int c = 0; vst3Info.units[c] != 0; ++c)
            unitStr += static_cast<char>(vst3Info.units[c]);
        m_paramUnits.push_back(unitStr);

        m_paramIDs.push_back(vst3Info.id);

        instruments::InstrumentParameterInfo info;
        info.name = m_paramNames.back().c_str();
        info.minValue = 0.0f;     // VST3 always normalized 0-1
        info.maxValue = 1.0f;
        info.defaultValue = static_cast<float>(vst3Info.defaultNormalizedValue);
        info.unit = m_paramUnits.back().c_str();
        info.isBoolean = (vst3Info.stepCount == 1);

        m_paramInfos.push_back(info);
    }
}

int VST3Instrument::parameterCount() const {
    return static_cast<int>(m_paramInfos.size());
}

const instruments::InstrumentParameterInfo& VST3Instrument::parameterInfo(int index) const {
    if (index < 0 || index >= static_cast<int>(m_paramInfos.size()))
        return s_emptyParam;
    return m_paramInfos[index];
}

float VST3Instrument::getParameter(int index) const {
    if (index < 0 || index >= static_cast<int>(m_paramIDs.size()) || !m_instance)
        return 0.0f;
    return static_cast<float>(m_instance->getParameterNormalized(m_paramIDs[index]));
}

void VST3Instrument::setParameter(int index, float value) {
    if (index < 0 || index >= static_cast<int>(m_paramIDs.size()) || !m_instance)
        return;
    m_instance->setParameterNormalized(m_paramIDs[index], static_cast<double>(value));
}

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
