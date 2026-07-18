#include "instruments/InstrumentRack.h"
#include "instruments/SubtractiveSynth.h"
#include "util/Factory.h"

namespace yawn {
namespace instruments {

InstrumentRack::InstrumentRack() = default;

void InstrumentRack::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    int stride = maxBlockSize * 2; // stereo interleaved
    m_chainBufHeap.resize(kMaxChains * stride, 0.0f);
    for (int i = 0; i < kMaxChains; ++i)
        m_chainBufPtrs[i] = m_chainBufHeap.data() + i * stride;
    for (int i = 0; i < m_numChains; ++i) {
        if (m_chains[i].instrument)
            m_chains[i].instrument->init(sampleRate, maxBlockSize);
        // Per-chain fx — only init if the chain was allocated by a
        // prior chainFxChain() call (or by the deserializer).
        // Newly-created chains start without an fx chain at all.
        if (m_chains[i].fx)
            m_chains[i].fx->init(sampleRate, maxBlockSize);
    }
}

void InstrumentRack::reset() {
    for (int i = 0; i < m_numChains; ++i) {
        if (m_chains[i].instrument)
            m_chains[i].instrument->reset();
        if (m_chains[i].fx)
            m_chains[i].fx->reset();
    }
}

void InstrumentRack::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Structural-edit handshake (see beginChainEdit): announce we're in
    // process(), then bail if a chain swap is underway. The seq_cst
    // store-before-load pairs with beginChainEdit's store-before-load
    // so the editor never frees a chain we're about to touch.
    m_inProcess.store(true, std::memory_order_seq_cst);
    if (m_rebuilding.load(std::memory_order_seq_cst)) {
        m_inProcess.store(false, std::memory_order_seq_cst);
        return;  // chains being rebuilt — emit nothing this block
    }

    for (int c = 0; c < m_numChains; ++c) {
        auto& chain = m_chains[c];
        if (!chain.enabled || !chain.instrument) continue;

        // Filter MIDI for this chain's key/velocity range
        m_chainMidi[c].clear();
        for (int i = 0; i < midi.count(); ++i) {
            const auto& msg = midi[i];
            if (msg.isNoteOn()) {
                uint8_t vel7 = midi::Convert::vel16to7(msg.velocity);
                if (msg.note >= chain.keyLow  && msg.note <= chain.keyHigh &&
                    vel7  >= chain.velLow  && vel7  <= chain.velHigh)
                    m_chainMidi[c].addMessage(msg);
            } else if (msg.isNoteOff()) {
                if (msg.note >= chain.keyLow && msg.note <= chain.keyHigh)
                    m_chainMidi[c].addMessage(msg);
            } else {
                m_chainMidi[c].addMessage(msg); // CC, PB pass through
            }
        }

        // Clear chain scratch buffer and render
        std::memset(m_chainBufPtrs[c], 0,
                    numFrames * numChannels * sizeof(float));
        chain.instrument->process(
            m_chainBufPtrs[c], numFrames, numChannels, m_chainMidi[c]);

        // Per-chain fx — process the scratch buffer in place after
        // the instrument fills it but BEFORE we apply chain
        // volume/pan, so the effects see the instrument's natural
        // signal level (compressors / saturation feel right). Same
        // pre-mix routing as DrumRack PadFx. Pinned per block — the
        // chain is lazy-created/cleared on the UI thread outside the
        // beginChainEdit handshake.
        auto fxRef = std::atomic_load_explicit(&chain.fx, std::memory_order_acquire);
        if (fxRef)
            fxRef->process(m_chainBufPtrs[c], numFrames, numChannels);

        // Mix into output with volume/pan
        float angle = (chain.pan + 1.0f) * 0.25f * (float)M_PI;
        float gL = chain.volume * std::cos(angle);
        float gR = chain.volume * std::sin(angle);
        for (int i = 0; i < numFrames; ++i) {
            buffer[i * numChannels + 0] +=
                m_chainBufPtrs[c][i * numChannels + 0] * gL;
            if (numChannels > 1)
                buffer[i * numChannels + 1] +=
                    m_chainBufPtrs[c][i * numChannels + 1] * gR;
        }
    }

    m_inProcess.store(false, std::memory_order_seq_cst);
}

nlohmann::json InstrumentRack::saveExtraState(
        const std::filesystem::path& assetDir) const {
    nlohmann::json j;
    nlohmann::json chains = nlohmann::json::array();
    for (int i = 0; i < m_numChains; ++i) {
        const auto& ch = m_chains[i];
        nlohmann::json cj;
        cj["keyLow"]  = ch.keyLow;  cj["keyHigh"] = ch.keyHigh;
        cj["velLow"]  = ch.velLow;  cj["velHigh"] = ch.velHigh;
        cj["volume"]  = ch.volume;  cj["pan"]     = ch.pan;
        cj["enabled"] = ch.enabled;
        if (ch.instrument) {
            nlohmann::json ij;
            ij["id"] = ch.instrument->id();
            nlohmann::json params = nlohmann::json::object();
            for (int p = 0; p < ch.instrument->parameterCount(); ++p) {
                const auto& info = ch.instrument->parameterInfo(p);
                if (info.isPerVoice) continue;
                params[info.name] = ch.instrument->getParameter(p);
            }
            ij["params"] = params;
            // Nested extra state (e.g. a sample-backed sub-instrument).
            nlohmann::json extra = ch.instrument->saveExtraState(assetDir);
            if (!extra.is_null() && !extra.empty()) ij["extra"] = extra;
            cj["instrument"] = ij;
        }
        chains.push_back(cj);
    }
    j["chains"] = chains;
    j["rackVolume"] = m_rackVolume;
    return j;
}

void InstrumentRack::loadExtraState(const nlohmann::json& state,
                                    const std::filesystem::path& assetDir) {
    if (!state.contains("chains")) return;
    // Park the audio thread (emits silence) while we free the old
    // sub-instruments and build the new ones — otherwise process()
    // can dereference a chain instrument mid-free (use-after-free).
    beginChainEdit();
    clearChains();
    for (const auto& cj : state["chains"]) {
        std::unique_ptr<Instrument> inst;
        if (cj.contains("instrument")) {
            const auto& ij = cj["instrument"];
            std::string id = ij.value("id", std::string());
            inst = ::yawn::createInstrument(id);
            if (inst) {
                if (m_sampleRate > 0) inst->init(m_sampleRate, m_maxBlockSize);
                if (ij.contains("params")) {
                    for (int p = 0; p < inst->parameterCount(); ++p) {
                        const auto& info = inst->parameterInfo(p);
                        if (info.isPerVoice) continue;
                        if (ij["params"].contains(info.name))
                            inst->setParameter(p, ij["params"][info.name].get<float>());
                    }
                }
                if (ij.contains("extra"))
                    inst->loadExtraState(ij["extra"], assetDir);
            }
        }
        uint8_t kl = cj.value("keyLow",  static_cast<uint8_t>(0));
        uint8_t kh = cj.value("keyHigh", static_cast<uint8_t>(127));
        uint8_t vl = cj.value("velLow",  static_cast<uint8_t>(1));
        uint8_t vh = cj.value("velHigh", static_cast<uint8_t>(127));
        if (inst && addChain(std::move(inst), kl, kh, vl, vh)) {
            int ci = m_numChains - 1;
            m_chains[ci].volume  = cj.value("volume", 1.0f);
            m_chains[ci].pan     = cj.value("pan",    0.0f);
            m_chains[ci].enabled = cj.value("enabled", true);
        }
    }
    if (state.contains("rackVolume"))
        m_rackVolume = state["rackVolume"].get<float>();
    // Safety: never leave a rack with zero chains (silent + confusing).
    if (m_numChains == 0)
        addChain(std::make_unique<SubtractiveSynth>());
    endChainEdit();  // re-admit the audio thread
}

} // namespace instruments
} // namespace yawn
