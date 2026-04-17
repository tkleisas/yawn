#ifdef YAWN_HAS_VST3

#include "vst3/VST3Host.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/uid.h"

#if defined(__linux__)
#include "pluginterfaces/gui/iplugview.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace yawn {
namespace vst3 {

// ── UID conversion helpers ──

std::string uidToString(const Steinberg::TUID uid) {
    return VST3::UID::fromTUID(uid).toString(false);
}

bool stringToUID(const std::string& str, Steinberg::TUID uid) {
    auto parsed = VST3::UID::fromString(str, false);
    if (!parsed) return false;
    std::memcpy(uid, (*parsed).data(), sizeof(Steinberg::TUID));
    return true;
}

#if defined(__linux__)
// Minimal IRunLoop stub for YAWN's main host context.
//
// On Linux many plugins (Surge XT, JUCE-based) require the host to expose a
// Steinberg::Linux::IRunLoop via the host context during controller
// initialization — otherwise parameter setup or the editor can silently
// degrade. YAWN's main process doesn't yet drive plugin-registered FDs or
// timers (plugins that need a live run loop should open their editor, which
// runs in yawn_vst3_host with a real loop). This stub just accepts
// registrations so initialize() calls that query IRunLoop succeed.
class StubRunLoop : public Steinberg::Linux::IRunLoop {
public:
    Steinberg::tresult PLUGIN_API registerEventHandler(
        Steinberg::Linux::IEventHandler*, Steinberg::Linux::FileDescriptor) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API unregisterEventHandler(
        Steinberg::Linux::IEventHandler*) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API registerTimer(
        Steinberg::Linux::ITimerHandler*, Steinberg::Linux::TimerInterval) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API unregisterTimer(
        Steinberg::Linux::ITimerHandler*) override {
        return Steinberg::kResultOk;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (!obj) return Steinberg::kInvalidArgument;
        *obj = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(this);
            return Steinberg::kResultOk;
        }
        return Steinberg::kNoInterface;
    }
};

class LinuxHostApp : public Steinberg::Vst::HostApplication {
public:
    LinuxHostApp() {
        if (auto* pis = getPlugInterfaceSupport())
            pis->addPlugInterfaceSupported(Steinberg::Linux::IRunLoop::iid);
    }
    Steinberg::tresult PLUGIN_API queryInterface(const char* iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            static StubRunLoop stub;
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(&stub);
            return Steinberg::kResultOk;
        }
        return Steinberg::Vst::HostApplication::queryInterface(iid, obj);
    }
};
#endif

// ── Host context singleton ──

Steinberg::Vst::HostApplication& getHostContext() {
#if defined(__linux__)
    static LinuxHostApp instance;
#else
    static Steinberg::Vst::HostApplication instance;
#endif
    return instance;
}

// ── VST3ModuleHandle ──

bool VST3ModuleHandle::load(const std::string& path, std::string& error) {
    m_module = VST3::Hosting::Module::create(path, error);
    if (!m_module) return false;
    m_path = path;
    m_name = m_module->getName();
    return true;
}

std::vector<VST3PluginInfo> VST3ModuleHandle::enumerate() const {
    std::vector<VST3PluginInfo> result;
    if (!m_module) return result;

    auto& factory = m_module->getFactory();
    auto classInfos = factory.classInfos();

    for (const auto& ci : classInfos) {
        // Only list audio processor classes
        if (ci.category() != kVstAudioEffectClass) continue;

        VST3PluginInfo info;
        info.name = ci.name();
        info.vendor = ci.vendor();
        info.version = ci.version();
        info.category = ci.category();
        info.subcategories = ci.subCategoriesString();
        info.classIDString = uidToString(ci.ID().data());
        info.modulePath = m_path;

        // Check if it's an instrument
        auto& subs = ci.subCategories();
        for (const auto& s : subs) {
            if (s == "Instrument") {
                info.isInstrument = true;
                break;
            }
        }

        result.push_back(std::move(info));
    }
    return result;
}

// ── In-memory IBStream for state persistence ──

class MemoryStream : public Steinberg::IBStream {
public:
    MemoryStream() = default;
    explicit MemoryStream(const std::vector<uint8_t>& data) : m_data(data) {}

    Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes,
                                        Steinberg::int32* numBytesRead) override {
        Steinberg::int32 avail = static_cast<Steinberg::int32>(m_data.size()) - m_pos;
        Steinberg::int32 toRead = std::min(numBytes, avail);
        if (toRead > 0) {
            std::memcpy(buffer, m_data.data() + m_pos, toRead);
            m_pos += toRead;
        }
        if (numBytesRead) *numBytesRead = toRead;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes,
                                         Steinberg::int32* numBytesWritten) override {
        if (numBytes <= 0) {
            if (numBytesWritten) *numBytesWritten = 0;
            return Steinberg::kResultOk;
        }
        auto newSize = m_pos + numBytes;
        if (newSize > static_cast<Steinberg::int32>(m_data.size()))
            m_data.resize(newSize);
        std::memcpy(m_data.data() + m_pos, buffer, numBytes);
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode,
                                        Steinberg::int64* result) override {
        Steinberg::int64 newPos = 0;
        switch (mode) {
            case kIBSeekSet: newPos = pos; break;
            case kIBSeekCur: newPos = m_pos + pos; break;
            case kIBSeekEnd: newPos = static_cast<Steinberg::int64>(m_data.size()) + pos; break;
            default: return Steinberg::kInvalidArgument;
        }
        if (newPos < 0) return Steinberg::kInvalidArgument;
        m_pos = static_cast<Steinberg::int32>(newPos);
        if (result) *result = m_pos;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override {
        if (pos) *pos = m_pos;
        return Steinberg::kResultOk;
    }

    // IUnknown
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        auto r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    const std::vector<uint8_t>& data() const { return m_data; }

private:
    std::vector<uint8_t> m_data;
    Steinberg::int32 m_pos = 0;
    std::atomic<Steinberg::int32> m_refCount{1};
};

// ── VST3PluginInstance ──

VST3PluginInstance::~VST3PluginInstance() {
    if (m_isProcessing) setProcessing(false);
    if (m_isActive) setActive(false);

    if (m_controller && !m_controllerIsComponent)
        m_controller->terminate();
    if (m_component)
        m_component->terminate();
}

std::unique_ptr<VST3PluginInstance> VST3PluginInstance::create(
    VST3::Hosting::Module::Ptr module,
    const std::string& classIDString,
    double sampleRate,
    int maxBlockSize)
{
    if (!module) return nullptr;

    auto optUid = VST3::UID::fromString(classIDString, false);
    if (!optUid) return nullptr;
    auto uid = *optUid;

    auto& factory = module->getFactory();

    // Create the processor component
    auto component = factory.createInstance<Steinberg::Vst::IComponent>(uid);
    if (!component) return nullptr;

    // Initialize component
    auto& hostCtx = getHostContext();
    if (component->initialize(&hostCtx) != Steinberg::kResultOk) {
        return nullptr;
    }

    // Get IAudioProcessor
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
    if (component->queryInterface(Steinberg::Vst::IAudioProcessor::iid,
                                   reinterpret_cast<void**>(&processor)) != Steinberg::kResultOk) {
        component->terminate();
        return nullptr;
    }

    // Get or create IEditController
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    bool controllerIsComponent = false;

    if (component->queryInterface(Steinberg::Vst::IEditController::iid,
                                   reinterpret_cast<void**>(&controller)) == Steinberg::kResultOk) {
        // Component implements both processor and controller
        controllerIsComponent = true;
    } else {
        // Separate controller — get its class ID from the component
        Steinberg::TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == Steinberg::kResultOk) {
            controller = factory.createInstance<Steinberg::Vst::IEditController>(
                VST3::UID::fromTUID(controllerCID));
            if (controller) {
                if (controller->initialize(&hostCtx) != Steinberg::kResultOk) {
                    controller = nullptr;
                }
            }
        }
    }

    // Build the instance
    auto inst = std::unique_ptr<VST3PluginInstance>(new VST3PluginInstance());
    inst->m_module = std::move(module);
    inst->m_component = std::move(component);
    inst->m_processor = std::move(processor);
    inst->m_controller = std::move(controller);
    inst->m_controllerIsComponent = controllerIsComponent;
    inst->m_classID = classIDString;

    // Query plugin name from factory
    auto classInfos = inst->m_module->getFactory().classInfos();
    for (const auto& ci : classInfos) {
        if (uidToString(ci.ID().data()) == classIDString) {
            inst->m_name = ci.name();
            auto& subs = ci.subCategories();
            for (const auto& s : subs) {
                if (s == "Instrument") {
                    inst->m_isInstrument = true;
                    break;
                }
            }
            break;
        }
    }

    // Query bus info
    inst->initBusInfo();

    // Setup processing
    if (!inst->setupProcessing(sampleRate, maxBlockSize)) {
        return nullptr;
    }

    // Activate
    if (!inst->setActive(true)) {
        return nullptr;
    }

    return inst;
}

bool VST3PluginInstance::initBusInfo() {
    if (!m_component) return false;

    // Enumerate every audio output bus and activate it. Plugins like DPF
    // assert if the host passes ProcessData with fewer buses than declared,
    // so we must provide buffers for every bus (even aux/sidechain).
    Steinberg::int32 numOutBuses = m_component->getBusCount(
        Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    m_outputBusChannels.clear();
    m_outputBusChannels.reserve(numOutBuses);
    for (Steinberg::int32 i = 0; i < numOutBuses; ++i) {
        Steinberg::Vst::BusInfo busInfo{};
        if (m_component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput,
                                     i, busInfo) == Steinberg::kResultOk) {
            m_outputBusChannels.push_back(busInfo.channelCount);
        } else {
            m_outputBusChannels.push_back(0);
        }
        m_component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, true);
    }
    if (!m_outputBusChannels.empty())
        m_numOutputChannels = m_outputBusChannels[0];

    // Audio input buses — same treatment.
    Steinberg::int32 numInBuses = m_component->getBusCount(
        Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    m_inputBusChannels.clear();
    m_inputBusChannels.reserve(numInBuses);
    for (Steinberg::int32 i = 0; i < numInBuses; ++i) {
        Steinberg::Vst::BusInfo busInfo{};
        if (m_component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput,
                                     i, busInfo) == Steinberg::kResultOk) {
            m_inputBusChannels.push_back(busInfo.channelCount);
        } else {
            m_inputBusChannels.push_back(0);
        }
        m_component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true);
    }
    if (!m_inputBusChannels.empty())
        m_numInputChannels = m_inputBusChannels[0];

    // Event input bus (MIDI)
    Steinberg::int32 numEventInBuses = m_component->getBusCount(
        Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    if (numEventInBuses > 0) {
        m_hasEventInput = true;
        m_component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, true);
    }

    return true;
}

bool VST3PluginInstance::connectComponents() {
    // Connection proxy between processor and controller can be set up here
    // For now, we skip this — it's needed for message passing between
    // separated processor/controller but not required for basic hosting
    return true;
}

bool VST3PluginInstance::setupProcessing(double sampleRate, int maxBlockSize) {
    if (!m_processor) return false;

    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;

    if (m_processor->setupProcessing(setup) != Steinberg::kResultOk)
        return false;

    // Allocate scratch channel buffers for every declared audio bus. Buffers
    // are owned by the instance so buildOutputBuses/buildInputBuses can hand
    // out stable float** pointers from the audio thread with zero allocation.
    auto allocateScratch = [maxBlockSize](
        const std::vector<int>& busChannels,
        std::vector<std::vector<float>>& storage,
        std::vector<std::vector<float*>>& scratchPtrs,
        std::vector<std::vector<float*>>& livePtrs)
    {
        int totalChannels = 0;
        for (int c : busChannels) totalChannels += c;
        storage.assign(totalChannels, std::vector<float>(maxBlockSize, 0.0f));
        scratchPtrs.clear();
        scratchPtrs.reserve(busChannels.size());
        livePtrs.clear();
        livePtrs.reserve(busChannels.size());
        int nextChannel = 0;
        for (int c : busChannels) {
            std::vector<float*> scratch;
            scratch.reserve(c);
            for (int k = 0; k < c; ++k) {
                scratch.push_back(storage[nextChannel++].data());
            }
            livePtrs.emplace_back(scratch);  // seed live == scratch
            scratchPtrs.push_back(std::move(scratch));
        }
    };

    allocateScratch(m_outputBusChannels, m_outputScratchStorage,
                    m_outputScratchPtrs, m_outputBusPtrs);
    allocateScratch(m_inputBusChannels,  m_inputScratchStorage,
                    m_inputScratchPtrs,  m_inputBusPtrs);
    m_maxBlockSize = maxBlockSize;

    return true;
}

namespace {
void buildBuses(const std::vector<int>& busChannels,
                std::vector<std::vector<float*>>& scratchPtrs,
                std::vector<std::vector<float*>>& livePtrs,
                Steinberg::Vst::AudioBusBuffers* outBuses,
                float** mainChannels, int mainChannelCount, int numSamples)
{
    const int numBuses = static_cast<int>(busChannels.size());
    for (int i = 0; i < numBuses; ++i) {
        const int channels = busChannels[i];
        auto& live = livePtrs[i];
        const auto& scratch = scratchPtrs[i];

        // Reset live pointers to scratch; silence the scratch for this block.
        for (int k = 0; k < channels; ++k) {
            live[k] = scratch[k];
            std::memset(scratch[k], 0,
                        static_cast<size_t>(numSamples) * sizeof(float));
        }

        // Main bus (index 0) gets caller buffers. Extra bus channels stay on
        // scratch silence.
        if (i == 0 && mainChannels && mainChannelCount > 0) {
            const int n = std::min(channels, mainChannelCount);
            for (int k = 0; k < n; ++k) live[k] = mainChannels[k];
        }

        outBuses[i].numChannels = channels;
        outBuses[i].silenceFlags = 0;
        outBuses[i].channelBuffers32 = channels > 0 ? live.data() : nullptr;
    }
}
} // namespace

void VST3PluginInstance::buildOutputBuses(Steinberg::Vst::AudioBusBuffers* outBuses,
                                           float** mainChannels,
                                           int mainChannelCount,
                                           int numSamples)
{
    buildBuses(m_outputBusChannels, m_outputScratchPtrs, m_outputBusPtrs,
               outBuses, mainChannels, mainChannelCount, numSamples);
}

void VST3PluginInstance::buildInputBuses(Steinberg::Vst::AudioBusBuffers* inBuses,
                                          float** mainChannels,
                                          int mainChannelCount,
                                          int numSamples)
{
    buildBuses(m_inputBusChannels, m_inputScratchPtrs, m_inputBusPtrs,
               inBuses, mainChannels, mainChannelCount, numSamples);
}

bool VST3PluginInstance::setActive(bool active) {
    if (!m_component) return false;
    // Caveat: setActive/setProcessing take a TBool (0 or 1) — NOT a tresult.
    // kResultTrue is 0 in the SDK and kResultFalse is 1, so passing those
    // inverts the meaning and leaves the plugin uninitialized (JUCE plugins
    // never run prepareToPlay, crashing later when MIDI arrives).
    auto result = m_component->setActive(active ? 1 : 0);
    if (result == Steinberg::kResultOk || result == Steinberg::kResultTrue)
        m_isActive = active;
    return m_isActive == active;
}

bool VST3PluginInstance::setProcessing(bool processing) {
    if (!m_processor) return false;
    auto result = m_processor->setProcessing(processing ? 1 : 0);
    if (result == Steinberg::kResultOk || result == Steinberg::kResultTrue)
        m_isProcessing = processing;
    return m_isProcessing == processing;
}

int VST3PluginInstance::parameterCount() const {
    return m_controller ? m_controller->getParameterCount() : 0;
}

Steinberg::Vst::ParameterInfo VST3PluginInstance::parameterInfo(int index) const {
    Steinberg::Vst::ParameterInfo info{};
    if (m_controller)
        m_controller->getParameterInfo(index, info);
    return info;
}

double VST3PluginInstance::getParameterNormalized(Steinberg::Vst::ParamID id) const {
    return m_controller ? m_controller->getParamNormalized(id) : 0.0;
}

void VST3PluginInstance::setParameterNormalized(Steinberg::Vst::ParamID id, double value) {
    if (m_controller)
        m_controller->setParamNormalized(id, value);
    // Queue for audio processor
    std::lock_guard<std::mutex> lock(m_paramMutex);
    m_pendingParams.push_back({id, value});
}

void VST3PluginInstance::drainParameterChanges(Steinberg::Vst::ParameterChanges& target) {
    std::vector<PendingParam> changes;
    {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        changes.swap(m_pendingParams);
    }
    target.clearQueue();
    for (auto& c : changes) {
        Steinberg::int32 index;
        auto* queue = target.addParameterData(c.id, index);
        if (queue) {
            Steinberg::int32 pointIndex;
            queue->addPoint(0, c.value, pointIndex);
        }
    }
}

// ── State persistence ──

bool VST3PluginInstance::getProcessorState(std::vector<uint8_t>& outData) const {
    if (!m_component) return false;
    auto* stream = new MemoryStream();
    Steinberg::IPtr<Steinberg::IBStream> streamPtr(stream, false);
    if (m_component->getState(stream) != Steinberg::kResultOk) return false;
    outData = stream->data();
    return true;
}

bool VST3PluginInstance::setProcessorState(const std::vector<uint8_t>& data) {
    if (!m_component) return false;
    auto* stream = new MemoryStream(data);
    Steinberg::IPtr<Steinberg::IBStream> streamPtr(stream, false);
    if (m_component->setState(stream) != Steinberg::kResultOk) return false;

    // Also pass processor state to the controller
    if (m_controller) {
        auto* stream2 = new MemoryStream(data);
        Steinberg::IPtr<Steinberg::IBStream> streamPtr2(stream2, false);
        m_controller->setComponentState(stream2);
    }
    return true;
}

bool VST3PluginInstance::getControllerState(std::vector<uint8_t>& outData) const {
    if (!m_controller) return false;
    auto* stream = new MemoryStream();
    Steinberg::IPtr<Steinberg::IBStream> streamPtr(stream, false);
    if (m_controller->getState(stream) != Steinberg::kResultOk) return false;
    outData = stream->data();
    return true;
}

bool VST3PluginInstance::setControllerState(const std::vector<uint8_t>& data) {
    if (!m_controller) return false;
    auto* stream = new MemoryStream(data);
    Steinberg::IPtr<Steinberg::IBStream> streamPtr(stream, false);
    return m_controller->setState(stream) == Steinberg::kResultOk;
}

Steinberg::IPtr<Steinberg::IPlugView> VST3PluginInstance::createView() const {
    if (!m_controller) return nullptr;
    auto* view = m_controller->createView(Steinberg::Vst::ViewType::kEditor);
    return Steinberg::owned(view);
}

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
