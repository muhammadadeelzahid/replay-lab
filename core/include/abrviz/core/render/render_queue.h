#pragma once

/**
 * @file
 * @brief A background queue that renders the experienced video of runs with `ffmpeg`.
 */

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

/**
 * @brief A request to render one run.
 */
struct RenderRequest {
    std::shared_ptr<const RunData> run;  ///< the loaded run; a null run fails the job
};

/**
 * @brief The outcome of one render job.
 */
struct RenderResult {
    QString path;   ///< the rendered file in the cache; empty on failure
    QString error;  ///< reason for the failure; empty on success
    bool fromCache = false;  ///< the file was already in the cache; nothing was rendered
    int parts = 0;  ///< number of played segments in the plan
    int peakConcurrency = 0;  ///< most `ffmpeg` processes that ran at once
    double seconds = 0.0;     ///< wall time of the job

    /**
     * @brief Tells whether the render produced a file.
     *
     * @return True if `path` is set.
     */
    [[nodiscard]] bool ok() const { return !path.isEmpty(); }
};

/**
 * @brief Renders the experienced video of runs, one job at a time, on its own thread.
 *
 * `enqueue` returns at once. A `std::jthread` takes jobs from a queue guarded by a mutex and a
 * `std::condition_variable_any`, waiting on its stop token. A job transcodes every played
 * segment in parallel (one `ffmpeg` per segment on the queue's own thread pool, by default one
 * per core), joins the parts with the concat demuxer, and writes the result into the cache
 * through `QSaveFile`. The cache key is claimed for the whole job with a `ClaimGuard`, so a
 * failed or cancelled job releases it.
 *
 * Cancelling a job's future, or destroying the queue, stops its `ffmpeg` processes within
 * 100 ms. The destructor stops and joins the worker thread before anything it uses is
 * destroyed; jobs still waiting end cancelled. Neither copyable nor movable: the worker thread
 * holds `this`.
 */
class RenderQueue {
public:
    /**
     * @brief Settings of the queue.
     */
    struct Options {
        QString cacheDirectory;  ///< empty: the application's cache location
        QString ffmpeg = QStringLiteral("ffmpeg");  ///< program name or path
        int threads = 0;         ///< parallel `ffmpeg` processes; 0: one per core
    };

    /**
     * @brief Creates a queue with default options and starts its worker thread.
     */
    RenderQueue();

    /**
     * @brief Creates a queue and starts its worker thread.
     *
     * @param options Cache folder, `ffmpeg` program and number of parallel processes.
     */
    explicit RenderQueue(Options options);

    /**
     * @brief Stops the worker and waits for it and the pool.
     *
     * A running job sees the stop request, kills its `ffmpeg` processes and waits for its part
     * tasks before the thread ends. Jobs never started end cancelled.
     */
    ~RenderQueue();

    /** @brief Neither copyable nor movable: the worker thread holds `this`. */
    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;
    RenderQueue(RenderQueue&&) = delete;
    RenderQueue& operator=(RenderQueue&&) = delete;

    /**
     * @brief Queues a render.
     *
     * Returns at once; thread-safe.
     *
     * @param request The run to render.
     * @return A future that reports progress (one step per part, plus the join) and yields one
     *         result; a cancelled job yields none. A failure yields a result with `error` set.
     */
    [[nodiscard]] QFuture<RenderResult> enqueue(RenderRequest request);

    /**
     * @brief Returns the cache of rendered files.
     *
     * @return The cache.
     */
    [[nodiscard]] RenderCache& cache() { return cache_; }

    /**
     * @brief Returns the number of `ffmpeg` processes that may run at once.
     *
     * @return The pool's maximum thread count.
     */
    [[nodiscard]] int threadCount() const;

    /**
     * @brief Returns the file for `run` if it was rendered before, without queueing anything.
     *
     * @param run The loaded run.
     * @return The path, or nothing if it is not in the cache.
     */
    [[nodiscard]] std::optional<QString> cachedFile(const RunData& run);

private:
    /**
     * @brief A queued request and the promise of its future.
     */
    struct Job {
        RenderRequest request;  ///< the run to render
        /** @brief Promise behind the future returned by enqueue(). */
        std::shared_ptr<QPromise<RenderResult>> promise;
    };

    /**
     * @brief Body of the worker thread: takes jobs one at a time until a stop is requested.
     *
     * A job whose future was cancelled before it started is finished without processing.
     *
     * @param stop The worker's stop token.
     */
    void workerLoop(std::stop_token stop);

    /**
     * @brief Renders one job and adds its result to the promise.
     *
     * Uses the cache if the file exists; fails if another job holds the key. Adds no result if
     * the job is cancelled or the worker is stopped.
     *
     * @param job The job.
     * @param workerStop The worker's stop token.
     */
    void process(Job& job, std::stop_token workerStop);

    const Options options_;  ///< Settings given to the constructor.
    RenderCache cache_;      ///< Rendered files, in `options_.cacheDirectory` or the default.
    /** @brief Runs the `ffmpeg` part tasks; `options_.threads` threads, or one per core. */
    QThreadPool pool_;
    std::mutex mutex_;                  ///< Guards `jobs_`.
    std::condition_variable_any wake_;  ///< Signalled when a job is queued.
    std::deque<Job> jobs_;  ///< Requests waiting for the worker, oldest first.
    std::jthread worker_;   ///< Worker thread; declared last, so it is stopped and joined first.
};

} // namespace abrviz

Q_DECLARE_METATYPE(abrviz::RenderResult)
