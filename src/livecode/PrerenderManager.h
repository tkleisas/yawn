#pragma once

// PrerenderManager — worker-pool owner of the live-coding prerender
// system (docs/live-coding.md §4.2). Queue + N workers render
// value-copied PrerenderSpecs offline (fresh devices from the Factory
// tables, no PortAudio stream involvement); completed jobs are delivered
// on the UI thread through App-provided hooks, exactly like the existing
// stem-separation and video-export worker patterns.

#include "audio/Prerender.h"
#include "audio/AudioBuffer.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yawn {

class Project;
namespace audio { class AudioEngine; }

namespace livecode {

class PrerenderManager {
public:
    struct Target {
        std::string kind;    // "clip" | "sampler" | "granular" | "drumslop"
                             // | "vocoder" | "drumrack" | "library" | "file"
        int track = 0;
        int scene = 0;
        int pad = 0;         // kind="drumrack"
        std::string path;    // kind="file"
        std::string name;    // display name for the delivered asset
    };

    struct Job {
        uint64_t id = 0;
        audio::PrerenderSpec spec;
        Target target;

        std::atomic<float>  progress{0.0f};
        std::atomic<bool>   cancel{false};
        std::atomic<bool>   started{false};
        std::atomic<bool>   done{false};
        std::atomic<bool>   ok{false};
        std::shared_ptr<audio::AudioBuffer> result;   // worker → UI
        std::string error;
    };

    PrerenderManager() = default;
    ~PrerenderManager() { shutdown(); }

    PrerenderManager(const PrerenderManager&) = delete;
    PrerenderManager& operator=(const PrerenderManager&) = delete;

    void init(int workers = 2);
    void shutdown();

    // UI thread. Returns the job id (>0), or 0 on immediate rejection
    // (manager not running). The spec is value-copied.
    uint64_t enqueue(audio::PrerenderSpec spec, Target target);
    void cancel(uint64_t id);

    // UI thread, once per frame: delivers completed jobs through the
    // hooks and reaps them.
    void poll();

    int activeCount() const;   // queued + rendering
    int idleCount() const;     // completed, not yet delivered

    // ── Delivery hooks (App-provided; UI thread) ──
    // clip: put the buffer into the (track, scene) slot like the
    // stem-separation flow (in-memory buffer; project save persists it).
    // sampler/granular: load into the track's instrument namespace via
    // the existing publish/retire paths.
    // file: performed by the manager itself (util::saveAudioBuffer).
    // report: console/log callback (progress toasts are throttled by the
    // Lua side at this stage).
    std::function<bool(const std::shared_ptr<audio::AudioBuffer>&,
                       int track, int scene, const std::string& name)> deliverClip;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name, int track)> deliverSampler;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name, int track)> deliverGranular;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name, int track)> deliverDrumSlop;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name, int track)> deliverVocoder;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name, int track, int pad)> deliverDrumRackPad;
    std::function<bool(std::shared_ptr<audio::AudioBuffer>,
                       const std::string& name)> deliverLibrary;
    std::function<void(const std::string& msg)> report;

private:
    void workerLoop(int workerIdx);

    std::deque<std::shared_ptr<Job>> m_queue;   // UI pushes, workers pop
    std::vector<std::shared_ptr<Job>> m_jobs;   // all live jobs
    std::vector<std::thread> m_workers;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_shutdown = false;
    uint64_t m_nextId = 1;

    // Completed jobs awaiting UI delivery (filled by poll()).
    std::vector<std::shared_ptr<Job>> m_finished;
};

} // namespace livecode
} // namespace yawn
