#include "audio/Prerender.h"
#include "instruments/Instrument.h"
#include "effects/EffectChain.h"
#include "util/Factory.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>

namespace yawn {
namespace audio {

static constexpr int kRenderBlockSize = 256;

std::shared_ptr<AudioBuffer> renderDevice(
    const PrerenderSpec& spec,
    std::atomic<float>& progress,
    const std::atomic<bool>& cancel) {

    if (spec.device.empty()) return nullptr;
    auto inst = createInstrument(spec.device);
    if (!inst) {
        LOG_WARN("Prerender", "Unknown instrument id '%s'", spec.device.c_str());
        return nullptr;
    }

    const int channels = std::clamp(spec.channels, 1, 2);
    const double spb = spec.sampleRate * 60.0 /
                       std::max(spec.tempoBpm, 1.0);
    const double totalBeats = std::max(spec.lengthBeats, 0.0) +
                              std::max(spec.tailBeats, 0.0);
    const int64_t totalFrames =
        std::max<int64_t>(static_cast<int64_t>(totalBeats * spb), 1);

    inst->init(spec.sampleRate, kRenderBlockSize);

    effects::EffectChain chain;   // standalone chain; no retire list needed —
                                  // the worker owns every object outright.
    chain.init(spec.sampleRate, kRenderBlockSize);
    for (const auto& fxSpec : spec.fx) {
        auto fx = createAudioEffect(fxSpec.id);
        if (!fx) {
            LOG_WARN("Prerender", "Unknown effect id '%s'", fxSpec.id.c_str());
            continue;
        }
        chain.append(std::move(fx));
        auto* live = chain.effectAt(chain.count() - 1);
        if (live) {
            for (const auto& [idx, val] : fxSpec.params) {
                if (idx >= 0 && idx < live->parameterCount())
                    live->setParameter(idx, val);
            }
        }
    }

    auto out = std::make_shared<AudioBuffer>(
        channels, static_cast<int>(totalFrames));
    std::vector<float> block(kRenderBlockSize * channels, 0.0f);
    const double beatsInBlock =
        kRenderBlockSize * (spec.tempoBpm / 60.0) / spec.sampleRate;

    auto pushNoteEvents = [&](midi::MidiBuffer& mb, double blockStartBeat) {
        for (const auto& n : spec.notes) {
            // Note-on within [blockStart, blockStart + beatsInBlock)
            const double off = n.beat - blockStartBeat;
            if (off >= 0.0 && off < beatsInBlock && n.pitch >= 0 && n.pitch <= 127) {
                midi::MidiMessage m = midi::MidiMessage::noteOn16(
                    static_cast<uint8_t>(n.channel),
                    static_cast<uint8_t>(n.pitch),
                    static_cast<uint16_t>(std::clamp(n.vel, 0.0f, 1.0f) * 65535.0f));
                m.frameOffset =
                    static_cast<int32_t>(std::clamp(off * spb, 0.0,
                        static_cast<double>(kRenderBlockSize - 1)));
                mb.addMessage(m);
            }
            // Release (may land in a later block).
            if (n.durBeats > 0.0) {
                const double offOff = (n.beat + n.durBeats) - blockStartBeat;
                if (offOff >= 0.0 && offOff < beatsInBlock &&
                    n.pitch >= 0 && n.pitch <= 127) {
                    midi::MidiMessage m = midi::MidiMessage::noteOff(
                        static_cast<uint8_t>(n.channel),
                        static_cast<uint8_t>(n.pitch));
                    m.frameOffset =
                        static_cast<int32_t>(std::clamp(offOff * spb, 0.0,
                            static_cast<double>(kRenderBlockSize - 1)));
                    mb.addMessage(m);
                }
            }
        }
    };

    int64_t f = 0;
    while (f < totalFrames) {
        if (cancel.load(std::memory_order_relaxed)) return nullptr;

        const int frames = static_cast<int>(
            std::min<int64_t>(kRenderBlockSize, totalFrames - f));
        midi::MidiBuffer mb;
        const double blockStartBeat = static_cast<double>(f) / spb;
        pushNoteEvents(mb, blockStartBeat);
        if (spec.noteSource) {
            spec.noteSource(blockStartBeat, frames, mb);
            mb.sortByFrame();
        }

        std::fill(block.begin(), block.end(), 0.0f);
        inst->process(block.data(), frames, channels, mb);
        chain.process(block.data(), frames, channels);

        // De-interleave into the output (non-interleaved, channel-major).
        for (int fLocal = 0; fLocal < frames; ++fLocal) {
            const int64_t dst = f + fLocal;
            for (int c = 0; c < channels; ++c)
                out->sample(c, static_cast<int>(dst)) =
                    block[static_cast<size_t>(fLocal) * channels + c];
        }

        f += frames;
        progress.store(static_cast<float>(static_cast<double>(f) / totalFrames),
                       std::memory_order_relaxed);
    }
    return out;
}

} // namespace audio
} // namespace yawn
