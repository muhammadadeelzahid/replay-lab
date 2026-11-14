#pragma once

#include "abrviz/core/model/run_data.h"
#include "abrviz/core/render/render_cache.h"

#include <QFuture>
#include <QMetaType>
#include <QPromise>
#include <QString>
#include <QThreadPool>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace abrviz {

struct RenderRequest {
    std::shared_ptr<const RunData> run;
};

struct RenderResult {
    QString path;   ///< the rendered file in the cache; empty on failure
    QString error;
    bool fromCache = false;
    int parts = 0;
    int peakConcurrency = 0;  ///< most `ffmpeg` processes that ran at once
    double seconds = 0.0;

    [[nodiscard]] bool ok() const { return !path.isEmpty(); }
};

/// Renders the experienced video of runs, one job at a time, on its own thread.
///
/// `enqueue` returns at once. A `std::jthread` takes jobs from a queue guarded by a mutex and a
/// `std::condition_variable_any`, waiting on its stop token. A job transcodes every played
/// segment in parallel (one `ffmpeg` per segment on the queue's own thread pool, by default one
/// per core), joins the parts with the concat demuxer, and writes the result into the cache
/// through `QSaveFile`. The cache key is claimed for the whole job with a `ClaimGuard`, so a
/// failed or cancelled job releases it.
///
/// Cancelling a job's future, or destroying the queue, stops its `ffmpeg` processes within
/// 100 ms. The destructor stops and joins the worker thread before anything it uses is
/// destroyed; jobs still waiting end cancelled. Neither copyable nor movable: the worker thread
/// holds `this`.
class RenderQueue {
public:
    struct Options {
        QString cacheDirectory;  ///< empty: the application's cache location
        QString ffmpeg = QStringLiteral("ffmpeg");
        int threads = 0;         ///< parallel `ffmpeg` processes; 0: one per core
    };

    RenderQueue();
    explicit RenderQueue(Options options);
    ~RenderQueue();

    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;
    RenderQueue(RenderQueue&&) = delete;
    RenderQueue& operator=(RenderQueue&&) = delete;

    /// Queues a render. The future reports progress (one step per part, plus the join) and
    /// yields one result; a cancelled job yields none.
    [[nodiscard]] QFuture<RenderResult> enqueue(RenderRequest request);

    [[nodiscard]] RenderCache& cache() { return cache_; }
    [[nodiscard]] int threadCount() const;
    /// The file for `run` if it was rendered before, without queueing anything.
    [[nodiscard]] std::optional<QString> cachedFile(const RunData& run);

private:
    struct Job {
        RenderRequest request;
        std::shared_ptr<QPromise<RenderResult>> promise;
    };

    void workerLoop(std::stop_token stop);
    void process(Job& job, std::stop_token workerStop);

    const Options options_;
    RenderCache cache_;
    QThreadPool pool_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Job> jobs_;
    std::jthread worker_;  // last: destroyed (stopped and joined) first
};

} // namespace abrviz

Q_DECLARE_METATYPE(abrviz::RenderResult)
