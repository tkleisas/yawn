#pragma once

// VST3Host — Host context and plugin lifecycle management for VST3 hosting.
// Provides the IHostApplication that plugins query, and manages the full
// lifecycle of loading a .vst3 module, creating processor + controller,
// and setting up audio processing.

#ifdef YAWN_HAS_VST3

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace yawn {
namespace vst3 {

// Information about a discovered VST3 plugin class
struct VST3PluginInfo {
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;         // "Audio Module Class" for processors
    std::string subcategories;    // "Instrument|Synth", "Fx|Reverb", etc.
    std::string classIDString;    // UID as hex string for serialization
    std::string modulePath;       // Path to the .vst3 bundle
    bool isInstrument = false;    // true if subcategories contain "Instrument"
};

// Holds a loaded VST3 module and provides access to its factory
class VST3ModuleHandle {
public:
    bool load(const std::string& path, std::string& error);

    bool isLoaded() const { return m_module != nullptr; }
    const std::string& path() const { return m_path; }
    const std::string& name() const { return m_name; }

    // Enumerate all plugin classes in this module
    std::vector<VST3PluginInfo> enumerate() const;

    // Get the raw module (for creating instances)
    VST3::Hosting::Module::Ptr module() const { return m_module; }

private:
    VST3::Hosting::Module::Ptr m_module;
    std::string m_path;
    std::string m_name;
};

// A fully instantiated VST3 plugin: processor + controller, ready to process
class VST3PluginInstance {
public:
    ~VST3PluginInstance();

    // Create a plugin instance from a loaded module and class ID
    static std::unique_ptr<VST3PluginInstance> create(
        VST3::Hosting::Module::Ptr module,
        const std::string& classIDString,
        double sampleRate,
        int maxBlockSize);

    // Audio processing lifecycle
    bool setupProcessing(double sampleRate, int maxBlockSize);
    bool setActive(bool active);
    bool setProcessing(bool processing);

    // Access interfaces
    Steinberg::Vst::IComponent* component() const { return m_component.get(); }
    Steinberg::Vst::IAudioProcessor* processor() const { return m_processor.get(); }
    Steinberg::Vst::IEditController* controller() const { return m_controller.get(); }

    // Plugin identity
    const std::string& name() const { return m_name; }
    const std::string& classID() const { return m_classID; }
    bool isInstrument() const { return m_isInstrument; }

    // Bus info
    int numAudioInputChannels() const { return m_numInputChannels; }
    int numAudioOutputChannels() const { return m_numOutputChannels; }
    bool hasEventInput() const { return m_hasEventInput; }

    // Full bus layout (channel count per bus).
    const std::vector<int>& outputBusChannelCounts() const { return m_outputBusChannels; }
    const std::vector<int>& inputBusChannelCounts() const { return m_inputBusChannels; }

    // Build AudioBusBuffers[] for every output bus the plugin declared. Bus 0 uses
    // mainChannels (clamped/padded to the bus's actual channel count with scratch);
    // every other bus uses scratch. outBuses must point to an array sized to
    // outputBusChannelCounts().size(). Safe to call from the audio thread — all
    // buffers are pre-allocated in setupProcessing().
    void buildOutputBuses(Steinberg::Vst::AudioBusBuffers* outBuses,
                          float** mainChannels, int mainChannelCount,
                          int numSamples);

    // Same for inputs. Pass nullptr mainChannels if there's no caller-provided
    // input (e.g., instruments) — all input buses get silence scratch.
    void buildInputBuses(Steinberg::Vst::AudioBusBuffers* inBuses,
                         float** mainChannels, int mainChannelCount,
                         int numSamples);

    // Parameter access via IEditController
    int parameterCount() const;
    Steinberg::Vst::ParameterInfo parameterInfo(int index) const;
    double getParameterNormalized(Steinberg::Vst::ParamID id) const;
    void setParameterNormalized(Steinberg::Vst::ParamID id, double value);

    // Drain queued parameter changes into target (call from audio thread before process)
    void drainParameterChanges(Steinberg::Vst::ParameterChanges& target);

    // State persistence
    bool getProcessorState(std::vector<uint8_t>& outData) const;
    bool setProcessorState(const std::vector<uint8_t>& data);
    bool getControllerState(std::vector<uint8_t>& outData) const;
    bool setControllerState(const std::vector<uint8_t>& data);

    // Editor view (returns nullptr if plugin has no editor)
    Steinberg::IPtr<Steinberg::IPlugView> createView() const;

private:
    VST3PluginInstance() = default;

    bool initBusInfo();
    bool connectComponents();

    VST3::Hosting::Module::Ptr m_module;  // Keep module alive
    Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
    bool m_controllerIsComponent = false;  // true when QI found controller on component

    std::string m_name;
    std::string m_classID;
    bool m_isInstrument = false;
    bool m_isActive = false;
    bool m_isProcessing = false;

    int m_numInputChannels = 0;
    int m_numOutputChannels = 2;
    bool m_hasEventInput = false;

    // Per-bus channel count, populated in initBusInfo().
    std::vector<int> m_outputBusChannels;
    std::vector<int> m_inputBusChannels;

    // Scratch silence buffers, allocated in setupProcessing().
    // Flat [channel][sample]; per-bus channel pointer arrays live in m_output*ScratchPtrs.
    std::vector<std::vector<float>> m_outputScratchStorage;
    std::vector<std::vector<float>> m_inputScratchStorage;
    // Stable scratch pointers, one vector per bus.
    std::vector<std::vector<float*>> m_outputScratchPtrs;
    std::vector<std::vector<float*>> m_inputScratchPtrs;
    // Live channel pointer arrays handed to the plugin each process call.
    // Reset from scratch at the top of every call and optionally overlaid
    // with caller-provided main-bus channels.
    std::vector<std::vector<float*>> m_outputBusPtrs;
    std::vector<std::vector<float*>> m_inputBusPtrs;
    int m_maxBlockSize = 0;

    // Thread-safe parameter change queue (UI thread → audio thread)
    struct PendingParam {
        Steinberg::Vst::ParamID id;
        double value;
    };
    std::mutex m_paramMutex;
    std::vector<PendingParam> m_pendingParams;
};

// Singleton host context — provides IHostApplication to plugins
Steinberg::Vst::HostApplication& getHostContext();

// Convert a Steinberg UID to/from hex string
std::string uidToString(const Steinberg::TUID uid);
bool stringToUID(const std::string& str, Steinberg::TUID uid);

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
