#include "platform/render_worker.h"
#include <algorithm>
#include <chrono>

namespace davepdf::platform {

RenderWorker::RenderWorker(PdfDocument& document) : doc_(document), thread_([this] { run(); }) {}

RenderWorker::~RenderWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool RenderWorker::submit(const RenderJob& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_ && in_flight_.same_image(job)) return false;
    for (auto& queued : queue_) {
        if (queued.same_image(job)) {
            queued.priority = std::min(queued.priority, job.priority);
            queued.requested = job.requested;
            return false;
        }
    }
    queue_.push_back(job);
    wake_.notify_one();
    return true;
}

void RenderWorker::prune(unsigned long long frame, unsigned generation, int rotation) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const RenderJob& j) {
        return j.requested + 2 < frame || j.generation != generation || j.rotation != rotation;
    }), queue_.end());
}

void RenderWorker::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    done_.clear();
}

std::vector<RenderResult> RenderWorker::collect() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RenderResult> out;
    out.swap(done_);
    return out;
}

bool RenderWorker::is_queued(const RenderJob& job) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_ && in_flight_.same_image(job)) return true;
    for (const auto& queued : queue_) if (queued.same_image(job)) return true;
    return false;
}

size_t RenderWorker::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + (busy_ ? 1 : 0);
}

bool RenderWorker::idle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() && !busy_;
}

void RenderWorker::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [&] { return queue_.empty() && !busy_; });
}

void RenderWorker::run() {
    for (;;) {
        RenderJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            // Lowest priority number first; ties go to the most recent request.
            auto best = std::min_element(queue_.begin(), queue_.end(), [](const RenderJob& a, const RenderJob& b) {
                return a.priority != b.priority ? a.priority < b.priority : a.requested > b.requested;
            });
            job = *best;
            queue_.erase(best);
            in_flight_ = job;
            busy_ = true;
        }
        RenderResult result;
        result.job = job;
        const auto started = std::chrono::steady_clock::now();
        {
            std::string error;
            std::lock_guard<std::mutex> doc_lock(doc_.mutex());
            result.ok = doc_.is_open() && doc_.render(job.page, job.width, job.height, job.rotation, result.bgra, error);
        }
        if (result.ok && job.invert) {
            // Night reading: invert colour, keep alpha, off the frame loop.
            for (size_t i = 0; i + 3 < result.bgra.size(); i += 4) {
                result.bgra[i] = static_cast<unsigned char>(255 - result.bgra[i]);
                result.bgra[i + 1] = static_cast<unsigned char>(255 - result.bgra[i + 1]);
                result.bgra[i + 2] = static_cast<unsigned char>(255 - result.bgra[i + 2]);
            }
        }
        result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (result.ok) done_.push_back(std::move(result));
            busy_ = false;
        }
        idle_.notify_all();
    }
}

} // namespace davepdf::platform
