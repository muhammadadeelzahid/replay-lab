#include "abrviz/core/render/render_queue.h"

#include "abrviz/core/render/render_plan.h"
#include "abrviz/core/render/segment_transcoder.h"
#include "abrviz/core/util/process_guard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <chrono>
#include <vector>

namespace abrviz {

namespace {

QString defaultCacheDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("renders"));
}

/// What the part tasks of one job share. Each task writes only its own slot; `mutex` and
/// `changed` let the worker sleep until a task finishes.
struct PartsState {
    explicit PartsState(std::size_t count)
        : results(count)
    {
    }

    std::vector<TranscodeResult> results;
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t finished = 0;  // guarded by mutex
    bool failed = false;       // guarded by mutex
    std::atomic<int> running{0};
    std::atomic<int> peak{0};
};

/// Copies `from` into the cache file `to` through QSaveFile, so `to` is either complete or
/// untouched.
QString publish(const QString& from, const QString& to, const std::function<bool()>& stop)
{
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly))
        return QStringLiteral("could not read %1: %2").arg(from, in.errorString());
    QSaveFile out(to);
    if (!out.open(QIODevice::WriteOnly))
        return QStringLiteral("could not write %1: %2").arg(to, out.errorString());
    QByteArray buffer(1 << 20, Qt::Uninitialized);
    while (!in.atEnd()) {
        if (stop())
            return QStringLiteral("cancelled");  // QSaveFile discards the partial file
        const qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0 || out.write(buffer.constData(), n) != n)
            return QStringLiteral("could not write %1: %2").arg(to, out.errorString());
    }
    if (!out.commit())
        return QStringLiteral("could not write %1: %2").arg(to, out.errorString());
    return {};
}

} // namespace

RenderQueue::RenderQueue()
    : RenderQueue(Options{})
{
}

RenderQueue::RenderQueue(Options options)
    : options_(std::move(options))
    , cache_(options_.cacheDirectory.isEmpty() ? defaultCacheDirectory() : options_.cacheDirectory)
{
    pool_.setMaxThreadCount(options_.threads > 0 ? options_.threads
                                                 : std::max(1, QThread::idealThreadCount()));
    // Started last, once everything it uses exists.
    worker_ = std::jthread([this](std::stop_token stop) { workerLoop(stop); });
}

RenderQueue::~RenderQueue()
{
    // Stop the worker first: it uses every other member. A running job sees the stop request,
    // kills its ffmpeg processes and waits for its part tasks before the thread ends.
    worker_.request_stop();
    if (worker_.joinable())
        worker_.join();
    pool_.waitForDone();
    // Jobs never started: their promises end cancelled when destroyed.
    const std::scoped_lock lock(mutex_);
    jobs_.clear();
}

int RenderQueue::threadCount() const
{
    return pool_.maxThreadCount();
}

std::optional<QString> RenderQueue::cachedFile(const RunData& run)
{
    return cache_.lookup(makeRenderPlan(run).cacheKey(run.info.runId));
}

QFuture<RenderResult> RenderQueue::enqueue(RenderRequest request)
{
    auto promise = std::make_shared<QPromise<RenderResult>>();
    promise->start();
    QFuture<RenderResult> future = promise->future();
    {
        const std::scoped_lock lock(mutex_);
        jobs_.push_back(Job{std::move(request), std::move(promise)});
    }
    wake_.notify_one();
    return future;
}

void RenderQueue::workerLoop(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); }))
                return;  // stop requested
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (!job.promise->isCanceled())
            process(job, stop);
        job.promise->finish();
    }
}

void RenderQueue::process(Job& job, std::stop_token workerStop)
{
    QElapsedTimer elapsed;
    elapsed.start();
    QPromise<RenderResult>& promise = *job.promise;
    auto fail = [&](const QString& error) {
        RenderResult result;
        result.error = error;
        result.seconds = static_cast<double>(elapsed.elapsed()) / 1000.0;
        promise.addResult(result);
    };

    const auto& run = job.request.run;
    if (!run) {
        fail(QStringLiteral("no run"));
        return;
    }
    const RenderPlan plan = makeRenderPlan(*run);
    if (plan.parts.empty()) {
        fail(QStringLiteral("the run has no played segments"));
        return;
    }
    const auto partCount = static_cast<int>(plan.parts.size());
    promise.setProgressRange(0, partCount + 1);

    const QString key = plan.cacheKey(run->info.runId);
    if (const auto cached = cache_.lookup(key)) {
        promise.setProgressValue(partCount + 1);
        promise.addResult(RenderResult{*cached, {}, true, partCount, 0, 0.0});
        return;
    }
    std::optional<ClaimGuard> claim = cache_.tryClaim(key);
    if (!claim) {
        fail(QStringLiteral("%1 is already being rendered").arg(run->info.runId));
        return;
    }
    // From here on, every return releases the claim unless it was committed.

    if (!QDir().mkpath(cache_.directory())) {
        fail(QStringLiteral("could not create %1").arg(cache_.directory()));
        return;
    }
    const QTemporaryDir scratch;  // removed with everything in it when the job ends
    if (!scratch.isValid()) {
        fail(QStringLiteral("could not create a temporary folder: %1").arg(scratch.errorString()));
        return;
    }

    std::stop_source jobStop;
    auto shouldStop = [&] {
        return workerStop.stop_requested() || promise.isCanceled();
    };

    // One task per part. A task never waits for another task; the worker thread (not a pool
    // thread) waits for all of them.
    auto state = std::make_shared<PartsState>(plan.parts.size());
    QStringList partFiles;
    for (int i = 0; i < partCount; ++i) {
        const RenderPart& part = plan.parts[static_cast<std::size_t>(i)];
        const QString output = scratch.filePath(QStringLiteral("part_%1.mp4").arg(i, 4, 10, QLatin1Char('0')));
        partFiles << output;
        TranscodeJob command{options_.ffmpeg, transcodeArguments(plan, part, output)};
        [[maybe_unused]] auto task = QtConcurrent::run(
            &pool_, [state, i, command = std::move(command), token = jobStop.get_token()] {
                const int now = state->running.fetch_add(1, std::memory_order_relaxed) + 1;
                int peak = state->peak.load(std::memory_order_relaxed);
                while (now > peak && !state->peak.compare_exchange_weak(peak, now)) {
                }
                TranscodeResult result = SegmentTranscoder::transcode(command, token);
                state->running.fetch_sub(1, std::memory_order_relaxed);
                const std::scoped_lock lock(state->mutex);
                state->failed = state->failed || (!result.ok && !result.stopped);
                state->results[static_cast<std::size_t>(i)] = std::move(result);
                ++state->finished;
                state->changed.notify_all();
            });
    }

    {
        std::unique_lock lock(state->mutex);
        while (state->finished < plan.parts.size()) {
            state->changed.wait_for(lock, std::chrono::milliseconds(100));
            promise.setProgressValue(static_cast<int>(state->finished));
            // A failed part makes the rest pointless; a cancelled job stops them all.
            if (state->failed || shouldStop())
                jobStop.request_stop();
        }
    }
    if (shouldStop())
        return;  // cancelled: no result, the claim is released
    for (int i = 0; i < partCount; ++i) {
        const TranscodeResult& result = state->results[static_cast<std::size_t>(i)];
        if (!result.ok && !result.stopped) {
            fail(QStringLiteral("segment %1: %2").arg(plan.parts[static_cast<std::size_t>(i)].segment)
                     .arg(result.error));
            return;
        }
    }

    // Join the parts, then publish the file into the cache.
    const QString listFile = scratch.filePath(QStringLiteral("parts.txt"));
    {
        QFile list(listFile);
        if (!list.open(QIODevice::WriteOnly) || list.write(concatList(partFiles)) < 0) {
            fail(QStringLiteral("could not write %1").arg(listFile));
            return;
        }
    }
    const QString joined = scratch.filePath(QStringLiteral("experienced.mp4"));
    ProcessGuard concat;
    if (!concat.start(options_.ffmpeg, concatArguments(listFile, joined))) {
        fail(concat.error());
        return;
    }
    switch (concat.wait(shouldStop)) {
    case ProcessGuard::Outcome::Stopped:
        return;
    case ProcessGuard::Outcome::Failed:
        fail(QStringLiteral("joining the parts: %1 %2")
                 .arg(concat.error(), QString::fromUtf8(concat.output()).trimmed()));
        return;
    case ProcessGuard::Outcome::Finished:
        break;
    }

    const QString path = cache_.pathFor(key);
    if (const QString error = publish(joined, path, shouldStop); !error.isEmpty()) {
        if (!shouldStop())
            fail(error);
        return;
    }
    claim->commit(path);
    promise.setProgressValue(partCount + 1);
    promise.addResult(RenderResult{path, {}, false, partCount,
                                   state->peak.load(std::memory_order_relaxed),
                                   static_cast<double>(elapsed.elapsed()) / 1000.0});
}

} // namespace abrviz
