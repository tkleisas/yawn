#pragma once

// VST3Instrument — Wraps a VST3 instrument plugin as a YAWN Instrument.
// Handles buffer format conversion (interleaved ↔ non-interleaved),
// MIDI conversion (YAWN MidiBuffer → VST3 EventList), and parameter mapping.

#ifdef YAWN_HAS_VST3

#include "instruments/Instrument.h"
#include "vst3/VST3Host.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace vst3 {

class VST3Instrument : public instruments::Instrument {
public:
    VST3Instrument(const std::string& modulePath, const std::string& classIDString);
    ~VST3Instrument() override = default;

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return m_displayName.c_str(); }
    const char* id()   const override { return m_pluginID.c_str(); }

    int parameterCount() const override;
    const instruments::InstrumentParameterInfo& parameterInfo(int index) const override;
    float getParameter(int index) const override;
    void  setParameter(int index, float value) override;

    // Access to the underlying VST3 instance (for editor, state, etc.)
    VST3PluginInstance* instance() const { return m_instance.get(); }

    const std::string& modulePath() const { return m_modulePath; }
    const std::string& classIDString() const { return m_classIDString; }

private:
    void convertMidiToEventList(const midi::MidiBuffer& midi,
                                Steinberg::Vst::EventList& eventList);
    void buildParameterInfoCache();

    std::string m_modulePath;
    std::string m_classIDString;
    std::string m_displayName;
    std::string m_pluginID;    // "vst3:<classID>"

    std::unique_ptr<VST3PluginInstance> m_instance;

    // Parameter cache
    std::vector<instruments::InstrumentParameterInfo> m_paramInfos;
    std::vector<std::string> m_paramNames;  // Storage for name strings
    std::vector<std::string> m_paramUnits;
    std::vector<Steinberg::Vst::ParamID> m_paramIDs;

    // Pre-allocated processing buffers (non-interleaved)
    std::vector<float> m_outLeft;
    std::vector<float> m_outRight;

    // Pre-sized AudioBusBuffers arrays — resized in init() to match plugin layout.
    std::vector<Steinberg::Vst::AudioBusBuffers> m_outputBuses;
    std::vector<Steinberg::Vst::AudioBusBuffers> m_inputBuses;

    static const instruments::InstrumentParameterInfo s_emptyParam;
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
