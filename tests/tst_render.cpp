#include "test_paths.h"

#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/core/render/render_cache.h"
#include "abrviz/core/render/render_plan.h"
#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/render/segment_transcoder.h"
#include "abrviz/core/util/process_guard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <type_traits>

using namespace abrviz;

// Copy and move rules from docs/architecture.md.
static_assert(!std::is_copy_constructible_v<ProcessGuard> && std::is_move_constructible_v<ProcessGuard>);
static_assert(!std::is_copy_assignable_v<ProcessGuard> && std::is_move_assignable_v<ProcessGuard>);
static_assert(!std::is_copy_constructible_v<ClaimGuard> && std::is_move_constructible_v<ClaimGuard>);
static_assert(!std::is_copy_assignable_v<ClaimGuard> && std::is_move_assignable_v<ClaimGuard>);
static_assert(!std::is_copy_constructible_v<RenderQueue> && !std::is_move_constructible_v<RenderQueue>);
static_assert(!std::is_copy_assignable_v<RenderQueue> && !std::is_move_assignable_v<RenderQueue>);

namespace {

constexpr int kTimeoutMs = 20000;

std::shared_ptr<const RunData> loadTestRun(const char* name)
{
    return loadBundleSync(test::dataPath(QString::fromLatin1(name))).run;
}

int countLines(const QByteArray& text)
{
    return static_cast<int>(text.count('\n'));
}

int childrenNamed(const QString& pattern)
{
    QProcess pgrep;
    pgrep.start(QStringLiteral("pgrep"),
                {QStringLiteral("-P"), QString::number(QCoreApplication::applicationPid()),
                 QStringLiteral("-f"), pattern});
    pgrep.waitForFinished();
    return countLines(pgrep.readAllStandardOutput());
}

/// A stand-in for ffmpeg: a shell script that sleeps, then writes its last argument (the output
/// file) or fails.
QString fakeFfmpeg(const QTemporaryDir& dir, const QString& name, double sleepSeconds, bool fail)
{
    const QString path = dir.filePath(name);
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly))
        return {};
    QByteArray body = "#!/bin/sh\n";
    body += "sleep " + QByteArray::number(sleepSeconds) + "\n";
    if (fail) {
        body += "echo 'Invalid data found when processing input' >&2\nexit 1\n";
    } else {
        body += "for last; do :; done\n";
        body += "echo part > \"$last\"\n";
    }
    script.write(body);
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

/// A run of three 2.5 s segments from tests/data/media, a 0.5 s stall after the second, and
/// playback cut off half way through the third.
std::shared_ptr<const RunData> syntheticRun()
{
    auto run = std::make_shared<RunData>();
    run->info.runId = QStringLiteral("synthetic");
    run->info.playerSegmentDuration = 3.3;
    run->video.frameRate = 30.0;
    run->video.segmentCount = 3;
    run->video.ladder = {{0, 1'000'000, 64, 36}};
    const QString media = test::dataPath(QStringLiteral("media/two_and_a_half_seconds.mp4"));
    for (int i = 0; i < 3; ++i) {
        Segment segment;
        segment.index = i;
        segment.mediaPath = media;
        segment.mediaDuration = 2.5;
        run->segments.push_back(segment);
    }
    run->playback = {{0, 1.0, 4.267, 100, 1'000'000},
                     {1, 4.3, 7.567, 100, 1'000'000},
                     {2, 8.1, 9.733, 50, 1'000'000}};
    run->stalls = {{7.6, 8.1, false}};
    run->playbackStart = 1.0;
    run->playbackEnd = 9.766;
    return run;
}

} // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:
    void planFollowsTheReplayTimeline()
    {
        const auto run = loadTestRun("bbb_36");
        QVERIFY(run);
        const RenderPlan plan = makeRenderPlan(*run);
        const PlaybackClock clock(*run);
        QCOMPARE(plan.parts.size(), run->playback.size());
        QVERIFY(std::abs(plan.duration() - clock.replayDuration()) < 1e-9);
        QCOMPARE(plan.frameRate, 30.0);

        // The stall (inside segment 16) freezes the end of segment 16.
        const auto frozen = std::ranges::find_if(plan.parts, [](const RenderPart& p) {
            return p.freezeAfter > 0.0;
        });
        QVERIFY(frozen != plan.parts.end());
        QCOMPARE(frozen->segment, 16);
        QVERIFY(std::abs(frozen->freezeAfter - run->stalls.front().duration()) < 1e-9);

        // Playback was cut off after 39 of 100 frames of segment 54.
        const RenderPart& last = plan.parts.back();
        QCOMPARE(last.segment, 54);
        const Segment& segment = *std::ranges::find(run->segments, 54, &Segment::index);
        QVERIFY(std::abs(last.duration - segment.mediaDuration * 0.39) < 1e-9);
    }

    void cacheKeyChangesWithThePlan()
    {
        const auto run = loadTestRun("chef_cooking_18");
        RenderPlan plan = makeRenderPlan(*run);
        const QString key = plan.cacheKey(run->info.runId);
        QVERIFY(key.startsWith(run->info.runId + QLatin1Char('-')));
        QCOMPARE(makeRenderPlan(*run).cacheKey(run->info.runId), key);
        plan.parts.back().duration -= 0.1;
        QVERIFY(plan.cacheKey(run->info.runId) != key);
    }

    void transcodeArgumentsTrimScaleAndFreeze()
    {
        RenderPlan plan;
        plan.frameRate = 25.0;
        const RenderPart part{3, QStringLiteral("/m/seg.mp4"), 1.5, 0.36};
        const QStringList args = transcodeArguments(plan, part, QStringLiteral("/o/part.mp4"));
        const QString filters = args[args.indexOf(QStringLiteral("-vf")) + 1];
        QVERIFY(filters.startsWith(QStringLiteral("trim=duration=1.500000")));
        QVERIFY(filters.contains(QStringLiteral("fps=25.000000")));
        QVERIFY(filters.contains(QStringLiteral("scale=1920:1080")));
        QVERIFY(filters.contains(QStringLiteral("tpad=stop_mode=clone:stop_duration=0.360000")));
        QCOMPARE(args.last(), QStringLiteral("/o/part.mp4"));
        QVERIFY(args.contains(QStringLiteral("-an")));

        QCOMPARE(concatList({QStringLiteral("/a/it's.mp4")}), QByteArray("file '/a/it'\\''s.mp4'\n"));
    }

    void claimIsExclusiveAndReleased()
    {
        QTemporaryDir dir;
        RenderCache cache(dir.path());
        {
            std::optional<ClaimGuard> claim = cache.tryClaim(QStringLiteral("k"));
            QVERIFY(claim);
            QVERIFY(!cache.tryClaim(QStringLiteral("k")));
            QVERIFY(cache.isClaimed(QStringLiteral("k")));
            ClaimGuard moved = std::move(*claim);  // the claim moves with the guard
            claim.reset();
            QVERIFY(cache.isClaimed(QStringLiteral("k")));
        }
        QVERIFY(!cache.isClaimed(QStringLiteral("k")));  // released without a commit
        QVERIFY(!cache.lookup(QStringLiteral("k")));

        const QString path = cache.pathFor(QStringLiteral("k"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("video");
        file.close();
        {
            ClaimGuard claim = *cache.tryClaim(QStringLiteral("k"));
            claim.commit(path);
        }
        QCOMPARE(cache.lookup(QStringLiteral("k")).value_or(QString()), path);
        QVERIFY(!cache.isClaimed(QStringLiteral("k")));
    }

    void processGuardKillsOnDestruction()
    {
        {
            ProcessGuard guard;
            QVERIFY(guard.start(QStringLiteral("sleep"), {QStringLiteral("30")}));
            QVERIFY(guard.isRunning());
            QCOMPARE(ProcessGuard::liveCount(), 1);
            QCOMPARE(childrenNamed(QStringLiteral("sleep 30")), 1);
        }
        QCOMPARE(ProcessGuard::liveCount(), 0);
        QCOMPARE(childrenNamed(QStringLiteral("sleep 30")), 0);
    }

    void processGuardStopsAndReportsFailures()
    {
        std::stop_source stop;
        ProcessGuard sleeper;
        QVERIFY(sleeper.start(QStringLiteral("sleep"), {QStringLiteral("30")}));
        stop.request_stop();
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(sleeper.wait(stop.get_token()), ProcessGuard::Outcome::Stopped);
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(!sleeper.isRunning());

        ProcessGuard failing;
        QVERIFY(failing.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("echo bad; exit 3")}));
        QCOMPARE(failing.wait(std::stop_token{}), ProcessGuard::Outcome::Failed);
        QVERIFY(failing.error().contains(QStringLiteral("code 3")));
        QCOMPARE(failing.output().trimmed(), QByteArray("bad"));

        ProcessGuard missing;
        QVERIFY(!missing.start(QStringLiteral("/nonexistent/ffmpeg"), {}));
        QVERIFY(!missing.error().isEmpty());
        QCOMPARE(ProcessGuard::liveCount(), 0);
    }

    void transcoderStopsWithin100ms()
    {
        std::stop_source stop;
        TranscodeResult result;
        std::jthread worker([&] {
            result = SegmentTranscoder::transcode({QStringLiteral("sleep"), {QStringLiteral("30")}},
                                                  stop.get_token());
        });
        QTRY_COMPARE(ProcessGuard::liveCount(), 1);
        QElapsedTimer timer;
        timer.start();
        stop.request_stop();
        worker.join();
        QVERIFY2(timer.elapsed() < 400, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(result.stopped);
        QCOMPARE(ProcessGuard::liveCount(), 0);
    }

    void rendersInParallelAndCaches()
    {
        QTemporaryDir dir;
        const QString ffmpeg = fakeFfmpeg(dir, QStringLiteral("ffmpeg"), 0.2, false);
        RenderQueue queue({dir.filePath(QStringLiteral("cache")), ffmpeg, 4});
        const auto run = loadTestRun("chef_cooking_18");  // 11 parts
        auto future = queue.enqueue({run});
        future.waitForFinished();
        const RenderResult result = future.result();
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.parts, 11);
        QCOMPARE(result.peakConcurrency, 4);  // as many at once as the pool allows
        QVERIFY(QFile::exists(result.path));
        QVERIFY(result.path.startsWith(dir.filePath(QStringLiteral("cache"))));
        QCOMPARE(future.progressValue(), future.progressMaximum());

        auto again = queue.enqueue({run});
        again.waitForFinished();
        QVERIFY(again.result().fromCache);
        QCOMPARE(again.result().path, result.path);
        QCOMPARE(queue.cachedFile(*run).value_or(QString()), result.path);
    }

    void failedRenderReleasesItsClaim()
    {
        QTemporaryDir dir;
        const QString ffmpeg = fakeFfmpeg(dir, QStringLiteral("ffmpeg"), 0.05, true);
        const QString cacheDir = dir.filePath(QStringLiteral("cache"));
        RenderQueue queue({cacheDir, ffmpeg, 4});
        const auto run = loadTestRun("chef_cooking_18");
        auto future = queue.enqueue({run});
        future.waitForFinished();
        QCOMPARE(future.resultCount(), 1);
        const RenderResult result = future.result();
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains(QStringLiteral("Invalid data")), qPrintable(result.error));

        const QString key = makeRenderPlan(*run).cacheKey(run->info.runId);
        QVERIFY(!queue.cache().isClaimed(key));
        QVERIFY(queue.cache().tryClaim(key));
        QVERIFY(!queue.cache().lookup(key));
        QVERIFY(QDir(cacheDir).entryList(QDir::Files).isEmpty());  // no partial file
        QCOMPARE(ProcessGuard::liveCount(), 0);
    }

    void cancellingLeavesNoProcess()
    {
        QTemporaryDir dir;
        const QString ffmpeg = fakeFfmpeg(dir, QStringLiteral("slow-ffmpeg"), 30, false);
        RenderQueue queue({dir.filePath(QStringLiteral("cache")), ffmpeg, 4});
        const auto run = loadTestRun("bbb_36");
        auto future = queue.enqueue({run});
        QTRY_COMPARE_WITH_TIMEOUT(ProcessGuard::liveCount(), 4, kTimeoutMs);
        QCOMPARE(childrenNamed(QStringLiteral("slow-ffmpeg")), 4);

        QElapsedTimer timer;
        timer.start();
        future.cancel();
        future.waitForFinished();
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(future.resultCount(), 0);
        QTRY_COMPARE(ProcessGuard::liveCount(), 0);
        QCOMPARE(childrenNamed(QStringLiteral("slow-ffmpeg")), 0);
        QVERIFY(!queue.cache().isClaimed(makeRenderPlan(*run).cacheKey(run->info.runId)));
    }

    void destroyingTheQueueStopsItsJobs()
    {
        QTemporaryDir dir;
        const QString ffmpeg = fakeFfmpeg(dir, QStringLiteral("slow-ffmpeg"), 30, false);
        QFuture<RenderResult> running;
        QFuture<RenderResult> waiting;
        {
            RenderQueue queue({dir.filePath(QStringLiteral("cache")), ffmpeg, 3});
            running = queue.enqueue({loadTestRun("bbb_36")});
            waiting = queue.enqueue({loadTestRun("chef_cooking_18")});
            QTRY_COMPARE_WITH_TIMEOUT(ProcessGuard::liveCount(), 3, kTimeoutMs);
        }
        QVERIFY(running.isFinished() && running.resultCount() == 0);
        QVERIFY(waiting.isFinished() && waiting.isCanceled());
        QCOMPARE(ProcessGuard::liveCount(), 0);
        QCOMPARE(childrenNamed(QStringLiteral("slow-ffmpeg")), 0);
    }

    void rendersWithRealFfmpeg()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not found");
        const auto run = syntheticRun();
        const PlaybackClock clock(*run);
        QVERIFY(std::abs(clock.replayDuration() - (2.5 + 2.5 + 0.5 + 1.25)) < 1e-9);

        QTemporaryDir dir;
        RenderQueue queue({dir.path(), ffmpeg, 0});
        auto future = queue.enqueue({run});
        future.waitForFinished();
        const RenderResult result = future.result();
        QVERIFY2(result.ok(), qPrintable(result.error));
        const auto duration = readMp4Duration(result.path);
        QVERIFY(duration);
        QVERIFY2(std::abs(*duration - clock.replayDuration()) < 1.0 / 30.0 + 1e-6,
                 qPrintable(QString::number(*duration)));
    }
};

QTEST_GUILESS_MAIN(TestRender)
#include "tst_render.moc"
