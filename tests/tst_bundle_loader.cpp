#include "abrviz/core/load/bundle_loader.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/util/future_guard.h"
#include "test_paths.h"

#include <QFutureWatcher>
#include <QRandomGenerator>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <type_traits>

using namespace abrviz;

static_assert(!std::is_copy_constructible_v<FutureGuard<int>>);
static_assert(!std::is_copy_assignable_v<FutureGuard<int>>);
static_assert(std::is_nothrow_move_constructible_v<FutureGuard<int>>);
static_assert(std::is_nothrow_move_assignable_v<FutureGuard<int>>);

namespace {

constexpr int kTimeoutMs = 60000;

bool waitFor(const QFuture<LoadResult>& future)
{
    return QTest::qWaitFor([&] { return future.isFinished(); }, kTimeoutMs);
}

/// The parallel loader must produce exactly what the synchronous reader produces.
void compareRuns(const RunData& a, const RunData& b)
{
    QCOMPARE(a.info.runId, b.info.runId);
    QCOMPARE(a.segments.size(), b.segments.size());
    for (std::size_t i = 0; i < a.segments.size(); ++i) {
        QCOMPARE(a.segments[i].playedRung, b.segments[i].playedRung);
        QCOMPARE(a.segments[i].bytes, b.segments[i].bytes);
        QCOMPARE(a.segments[i].mediaDuration, b.segments[i].mediaDuration);
        QCOMPARE(a.segments[i].played, b.segments[i].played);
    }
    QCOMPARE(a.decisions.size(), b.decisions.size());
    for (std::size_t i = 0; i < a.decisions.size(); ++i) {
        QCOMPARE(a.decisions[i].llmRung, b.decisions[i].llmRung);
        QCOMPARE(a.decisions[i].bufferFullWait, b.decisions[i].bufferFullWait);
    }
    QCOMPARE(a.buffer.size(), b.buffer.size());
    QCOMPARE(a.buffer.back().time, b.buffer.back().time);
    QCOMPARE(a.playback.size(), b.playback.size());
    for (std::size_t i = 0; i < a.playback.size(); ++i) {
        QCOMPARE(a.playback[i].segment, b.playback[i].segment);
        QCOMPARE(a.playback[i].framesPlayed, b.playback[i].framesPlayed);
    }
    QCOMPARE(a.stalls.size(), b.stalls.size());
    QCOMPARE(a.warnings.size(), b.warnings.size());
}

QStringList bundleDirectories()
{
    if (test::haveFullBundles()) {
        const QDir root(test::bundlesPath());
        QStringList result;
        for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            result.append(root.filePath(name));
        return result;
    }
    return {test::dataPath(QStringLiteral("chef_cooking_18")),
            test::dataPath(QStringLiteral("bbb_36"))};
}

} // namespace

class TestBundleLoader : public QObject {
    Q_OBJECT

private slots:
    void sameResultAsSynchronousReader()
    {
        for (const QString& name : {QStringLiteral("chef_cooking_18"), QStringLiteral("bbb_36")}) {
            const QString dir = test::dataPath(name);
            const QFuture<LoadResult> future = loadBundle(dir);
            QVERIFY(waitFor(future));
            QCOMPARE(future.resultCount(), 1);
            const LoadResult parallel = future.result();
            const LoadResult serial = loadBundleSync(dir);
            QCOMPARE(parallel.errors, serial.errors);
            QVERIFY(parallel.run && serial.run);
            compareRuns(*parallel.run, *serial.run);
        }
    }

    void manyLoadsOnASmallPoolFinish()
    {
        // Every load fans out into many tasks. With only two threads, a task that waited for
        // another task would block the pool for good.
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        const QStringList dirs = bundleDirectories();
        QList<QFuture<LoadResult>> futures;
        for (const QString& dir : dirs)
            futures.append(loadBundle(dir, &pool));
        for (qsizetype i = 0; i < futures.size(); ++i) {
            QVERIFY2(waitFor(futures[i]), qPrintable(dirs[i]));
            QVERIFY2(futures[i].result().run, qPrintable(dirs[i]));
        }
        compareRuns(*futures.first().result().run, *loadBundleSync(dirs.first()).run);
    }

    void reportsProgress()
    {
        QFutureWatcher<LoadResult> watcher;
        int lastValue = -1;
        bool increasing = true;
        connect(&watcher, &QFutureWatcher<LoadResult>::progressValueChanged, this,
                [&](int value) {
                    increasing = increasing && value >= lastValue;
                    lastValue = value;
                });
        watcher.setFuture(loadBundle(test::dataPath(QStringLiteral("bbb_36"))));
        QVERIFY(QTest::qWaitFor([&] { return watcher.isFinished(); }, kTimeoutMs));
        QVERIFY(increasing);
        QVERIFY(watcher.progressMaximum() > 4);
        QCOMPARE(watcher.progressValue(), watcher.progressMaximum());
    }

    void missingBundleGivesErrors()
    {
        const auto future = loadBundle(test::dataPath(QStringLiteral("does_not_exist")));
        QVERIFY(waitFor(future));
        QVERIFY(!future.result().run);
        QVERIFY(!future.result().errors.isEmpty());
    }

    void cancelAtRandomPoints()
    {
        auto* random = QRandomGenerator::global();
        for (int i = 0; i < 30; ++i) {
            QFuture<LoadResult> future = loadBundle(test::dataPath(QStringLiteral("bbb_36")));
            QThread::usleep(random->bounded(20000u));
            future.cancel();
            QVERIFY(waitFor(future));
            QVERIFY(future.resultCount() <= 1);
        }
    }

    void futureGuardWaitsForTheTask()
    {
        QFuture<LoadResult> observed;
        {
            FutureGuard<LoadResult> guard(loadBundle(test::dataPath(QStringLiteral("bbb_36"))));
            observed = *guard.future();
        }  // the guard cancels and waits here
        QVERIFY(observed.isFinished());

        FutureGuard<LoadResult> a(loadBundle(test::dataPath(QStringLiteral("chef_cooking_18"))));
        FutureGuard<LoadResult> b(std::move(a));
        QVERIFY(!a.future());
        a = std::move(b);
        QVERIFY(a.future());
        const auto released = a.release();
        QVERIFY(released);
        QVERIFY(waitFor(*released));
        QCOMPARE(released->resultCount(), 1);
    }
};

QTEST_GUILESS_MAIN(TestBundleLoader)
#include "tst_bundle_loader.moc"
