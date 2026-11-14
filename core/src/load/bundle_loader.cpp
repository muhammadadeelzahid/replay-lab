#include "abrviz/core/load/bundle_loader.h"

#include "abrviz/core/io/mapped_file.h"
#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/io/text_lines.h"
#include "abrviz/core/load/bundle_reader.h"

#include <QPromise>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <memory>
#include <mutex>

namespace abrviz {

namespace {

/**
 * @brief State shared by every task of one load.
 *
 * Synchronisation uses only standard atomics and mutexes, so ThreadSanitizer can check it even
 * though Qt is not built with TSan:
 * - `ready` (release/acquire) publishes everything prepared before the tasks start;
 * - each task writes only its own member of `parts` or its own slot of `logParts`;
 * - `remaining` counts unfinished tasks; the task that brings it to zero (acq_rel) sees every
 *   other task's writes and assembles the result. No thread waits for another.
 */
struct LoadState {
    QPromise<LoadResult> promise;  ///< Reports progress and the result to the caller's future.
    BundleParts parts;             ///< What the tasks have read; each task writes its own member.
    QThreadPool* pool = nullptr;   ///< Pool the tasks run on.

    /** @brief Mapping of `sim.log`; `logChunks` are views into it. */
    std::shared_ptr<const MappedFile> simLog;
    QList<QByteArrayView> logChunks;   ///< Line-aligned pieces of `sim.log`, one task each.
    std::vector<SimLogPart> logParts;  ///< Parse result of each chunk; one slot per chunk.

    std::atomic<bool> ready{false};  ///< Set (release) once everything the tasks read is prepared.
    std::atomic<int> remaining{0};   ///< Unfinished tasks; the task that brings it to 0 assembles.

    std::mutex progressMutex;  ///< Keeps the reported progress increasing.
    int progress = 0;          ///< Steps done so far; guarded by `progressMutex`.

    /**
     * @brief Advances the reported progress by one step.
     *
     * Thread-safe; the mutex keeps the reported progress increasing.
     */
    void step()
    {
        const std::scoped_lock lock(progressMutex);
        promise.setProgressValue(++progress);
    }

    /**
     * @brief Tells whether the caller cancelled the load.
     *
     * @return True if the promise was cancelled.
     */
    [[nodiscard]] bool canceled() const { return promise.isCanceled(); }
};

/** @brief The load's shared state, held by every task of the load and freed after the last. */
using State = std::shared_ptr<LoadState>;

/**
 * @brief Reports the result, unless the load was cancelled, and finishes the promise.
 *
 * @param state The load.
 * @param result The result to report.
 */
void finish(const State& state, LoadResult result)
{
    if (!state->canceled())
        state->promise.addResult(std::move(result));
    state->promise.finish();
}

/**
 * @brief Merges the log chunks and assembles the result.
 *
 * Runs in whichever task finishes last. If the load was cancelled it only finishes the promise.
 *
 * @param state The load.
 */
void completeLoad(const State& state)
{
    if (state->canceled()) {
        state->promise.finish();
        return;
    }
    SimLogPart merged;
    for (const SimLogPart& part : state->logParts)
        SimLogParser::merge(merged, part);
    state->parts.simLog.value = std::move(merged);
    state->logParts.clear();
    state->simLog.reset();
    finish(state, assembleRunData(std::move(state->parts)));
}

/**
 * @brief Starts `work` on the pool.
 *
 * Every task counts one step of progress and one fewer task remaining, even when the load was
 * cancelled and the work skipped. The task that brings `remaining` to zero calls completeLoad().
 *
 * @tparam Work Callable taking no arguments.
 * @param state The load.
 * @param work The work of the task.
 */
template <typename Work>
void startTask(const State& state, Work work)
{
    QThreadPool* pool = state->pool;
    // The future is not needed: completion is tracked by `remaining`.
    [[maybe_unused]] auto future = QtConcurrent::run(pool, [state, work = std::move(work)]() mutable {
        [[maybe_unused]] const bool ready = state->ready.load(std::memory_order_acquire);
        Q_ASSERT(ready);
        if (!state->canceled()) {
            work();
            state->step();
        }
        if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            completeLoad(state);
    });
}

/**
 * @brief Maps `sim.log` and cuts it into line-aligned chunks, one task each.
 *
 * If the file cannot be mapped, an error is added to `parts.simLog` and there are no chunks.
 *
 * @param state The load; receives the mapping, the chunks and one empty slot per chunk.
 * @param chunks Wanted number of chunks.
 */
void prepareSimLog(LoadState& state, qsizetype chunks)
{
    QString error;
    auto mapping = MappedFile::open(simLogPath(state.parts.directory), &error);
    if (!mapping) {
        state.parts.simLog.addError(QStringLiteral("logs/sim.log"), 0,
                                    QStringLiteral("cannot read: %1").arg(error));
        return;
    }
    state.simLog = std::make_shared<const MappedFile>(std::move(*mapping));
    state.logChunks = splitAtLines(state.simLog->view(), chunks);
    state.logParts.resize(static_cast<std::size_t>(state.logChunks.size()));
}

/**
 * @brief Starts the parallel stage, once the run, video and segment records are known.
 *
 * Starts one task for each of `samples.jsonl`, `player_events.jsonl`, `summary.json` and
 * `policy.log`, one per `sim.log` chunk (twice the pool's thread count) and one per media file.
 *
 * @param state The load.
 */
void startParallelStage(const State& state)
{
    BundleParts& parts = state->parts;
    const QString dir = parts.directory;
    const int nodeId = parts.run.value.nodeId;
    const QStringList media = playedMediaPaths(parts.records.value, parts.video.info.value);
    prepareSimLog(*state, std::max(1, state->pool->maxThreadCount()) * 2);
    parts.mediaDurations.assign(static_cast<std::size_t>(media.size()), std::nullopt);

    constexpr int kFiles = 4;
    const auto tasks = static_cast<int>(kFiles + state->logChunks.size() + media.size());
    state->promise.setProgressRange(0, tasks);
    state->remaining.store(tasks, std::memory_order_relaxed);
    state->ready.store(true, std::memory_order_release);  // publish everything above

    startTask(state, [state, dir] { state->parts.samples = readSamples(dir); });
    startTask(state, [state, dir] { state->parts.events = readPlayerEvents(dir); });
    startTask(state, [state, dir] { state->parts.summary = readSummary(dir); });
    startTask(state, [state, dir, nodeId] { state->parts.policy = readPolicyLog(dir, nodeId); });

    const SimLogParser parser(nodeId);
    for (qsizetype i = 0; i < state->logChunks.size(); ++i) {
        startTask(state, [state, parser, i] {
            const auto slot = static_cast<std::size_t>(i);
            state->logParts[slot] = parser.parseChunk(state->logChunks[i]);
        });
    }
    for (qsizetype i = 0; i < media.size(); ++i) {
        startTask(state, [state, path = media[i], i] {
            state->parts.mediaDurations[static_cast<std::size_t>(i)] = readMp4Duration(path);
        });
    }
}

} // namespace

QFuture<LoadResult> loadBundle(const QString& directory, QThreadPool* pool)
{
    auto state = std::make_shared<LoadState>();
    state->pool = pool ? pool : QThreadPool::globalInstance();
    state->parts.directory = directory;
    state->promise.start();
    QFuture<LoadResult> future = state->promise.future();
    state->ready.store(true, std::memory_order_release);

    // Stage 1: the small files everything else depends on, read by one task.
    [[maybe_unused]] auto first = QtConcurrent::run(state->pool, [state] {
        [[maybe_unused]] const bool ready = state->ready.load(std::memory_order_acquire);
        if (state->canceled()) {
            state->promise.finish();
            return;
        }
        BundleParts& parts = state->parts;
        parts.run = readRunInfo(parts.directory);
        if (parts.run.ok()) {
            parts.video = readVideo(parts.directory, parts.run.value.videoName);
            parts.records = readSegmentRecords(parts.directory);
        }
        if (!parts.run.ok() || !parts.video.info.ok() || parts.records.value.empty()) {
            finish(state, assembleRunData(std::move(parts)));
            return;
        }
        startParallelStage(state);
    });
    return future;
}

} // namespace abrviz
