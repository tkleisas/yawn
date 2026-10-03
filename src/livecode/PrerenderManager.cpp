#include "livecode/PrerenderManager.h"
#include "audio/AudioEngine.h"
#include "app/Project.h"
#include "util/FileIO.h"
#include "util/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace yawn {
namespace livecode {

void PrerenderManager::init(int workers) {
    if (m_workers.empty()) {
        for (int i = 0; i < std::max(1, workers); ++i)
            m_workers.emplace_back([this, i] { workerLoop(i); });
    }
}

void PrerenderManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown) return;
        m_shutdown = true;
        for (auto& job : m_jobs) job->cancel.store(true);
    }
    m_cv.notify_all();
    for (auto& w : m_workers)
        if (w.joinable()) w.join();
    m_workers.clear();
    m_queue.clear();
    m_jobs.clear();
    m_finished.clear();
}

uint64_t PrerenderManager::enqueue(audio::PrerenderSpec spec, Target target) {
    auto job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown) return 0;
        job->id = m_nextId++;
        job->spec = std::move(spec);
        job->target = std::move(target);
        m_queue.push_back(job);
        m_jobs.push_back(job);
    }
    m_cv.notify_one();
    return job->id;
}

void PrerenderManager::cancel(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& job : m_jobs)
        if (job->id == id) job->cancel.store(true);
}

void PrerenderManager::poll() {
    // Move finished jobs out under the lock.
    m_finished.clear();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            auto& job = *it;
            if (job->done.load(std::memory_order_acquire) &&
                job->started.load(std::memory_order_acquire)) {
                m_finished.push_back(job);
                it = m_jobs.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& job : m_finished) {
        const bool ok = job->ok.load(std::memory_order_acquire);
        const std::string name = !job->target.name.empty() ? job->target.name
                                                           : job->spec.device;
        if (!ok) {
            if (report)
                report(job->error.empty()
                    ? "prerender: job " + std::to_string(job->id) + " failed"
                    : "prerender: " + job->error);
            continue;
        }
        auto buf = job->result;
        if (!buf) {
            if (report) report("prerender: job " + std::to_string(job->id) +
                               " produced no audio");
            continue;
        }
        bool delivered = false;
        if (job->target.kind == "clip") {
            if (deliverClip)
                delivered = deliverClip(buf, job->target.track,
                                        job->target.scene, name);
        } else if (job->target.kind == "sampler") {
            if (deliverSampler)
                delivered = deliverSampler(buf, name, job->target.track);
        } else if (job->target.kind == "granular") {
            if (deliverGranular)
                delivered = deliverGranular(buf, name, job->target.track);
        } else if (job->target.kind == "drumslop") {
            if (deliverDrumSlop)
                delivered = deliverDrumSlop(buf, name, job->target.track);
        } else if (job->target.kind == "vocoder") {
            if (deliverVocoder)
                delivered = deliverVocoder(buf, name, job->target.track);
        } else if (job->target.kind == "drumrack") {
            if (deliverDrumRackPad)
                delivered = deliverDrumRackPad(buf, name, job->target.track,
                                               job->target.pad);
        } else if (job->target.kind == "library") {
            if (deliverLibrary)
                delivered = deliverLibrary(buf, name);
        } else if (job->target.kind == "file") {
            const std::string path = job->target.path;
            if (!path.empty())
                delivered = util::saveAudioBuffer(
                    path, *buf, static_cast<int>(job->spec.sampleRate));
        } else {
            if (report) report("prerender: unknown target '" +
                               job->target.kind + "'");
        }
        if (report) {
            report(delivered
                ? "prerender: delivered '" + name + "' (" +
                      std::to_string(buf->numFrames()) + " frames → " +
                      job->target.kind + ")"
                : "prerender: delivery failed for '" + name + "'");
        }
    }
}

int PrerenderManager::activeCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_jobs.size());
}

int PrerenderManager::idleCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_queue.size());
}

void PrerenderManager::workerLoop(int workerIdx) {
    (void)workerIdx;
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_shutdown || !m_queue.empty();
            });
            if (m_shutdown) return;
            job = m_queue.front();
            m_queue.pop_front();
        }

        job->started.store(true, std::memory_order_release);
        auto result = audio::renderDevice(job->spec, job->progress, job->cancel);
        if (job->cancel.load()) {
            job->ok.store(false, std::memory_order_release);
            job->error = "prerender: job " + std::to_string(job->id) + " cancelled";
        } else if (!result) {
            job->ok.store(false, std::memory_order_release);
            job->error = "prerender: job " + std::to_string(job->id) +
                         " failed (unknown device '" + job->spec.device + "'?)";
        } else {
            job->result = std::move(result);
            job->ok.store(true, std::memory_order_release);
        }
        job->done.store(true, std::memory_order_release);
    }
}

} // namespace livecode
} // namespace yawn
