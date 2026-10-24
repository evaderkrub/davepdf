#pragma once
// A single background thread that turns page-render requests into pixel
// buffers. PDFium is not thread safe, so every call into it, here and on the
// main thread, happens under the document's mutex; this thread simply takes
// the long calls off the frame loop so the window keeps responding.
#include "platform/pdf_engine.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace davepdf::platform {

struct RenderJob {
    int page = 0, width = 0, height = 0, rotation = 0;
    unsigned generation = 0;
    bool invert = false;
    int priority = 0;                    // lower runs first
    unsigned long long requested = 0;    // frame index of the latest request
    bool same_image(const RenderJob& o) const {
        return page == o.page && width == o.width && height == o.height && rotation == o.rotation && generation == o.generation && invert == o.invert;
    }
};

struct RenderResult {
    RenderJob job;
    std::vector<unsigned char> bgra;
    double milliseconds = 0;
    bool ok = false;
};

class RenderWorker {
public:
    explicit RenderWorker(PdfDocument& document);
    ~RenderWorker();
    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    // Queues a job, or refreshes the priority and request frame of an
    // identical one already waiting. Returns false when it is already queued
    // or in flight.
    bool submit(const RenderJob& job);
    // Drops waiting jobs nobody asked for since `frame`, and any from another
    // generation or rotation. The job in flight always completes.
    void prune(unsigned long long frame, unsigned generation, int rotation);
    void cancel_all();
    // Finished renders, oldest first. Called once per frame on the main thread.
    std::vector<RenderResult> collect();
    bool is_queued(const RenderJob& job) const;
    size_t pending() const;      // waiting plus in flight
    bool idle() const;
    // Blocks until the queue is empty and nothing is in flight.
    void drain();

private:
    void run();
    PdfDocument& doc_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::vector<RenderJob> queue_;
    std::vector<RenderResult> done_;
    RenderJob in_flight_;
    bool busy_ = false;
    bool quit_ = false;
    std::thread thread_;
};

} // namespace davepdf::platform
