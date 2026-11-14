#include "test_paths.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/util/parallel_map.h"

#include <QFileInfo>
#include <QTest>
#include <QThreadPool>

#include <atomic>
#include <thread>

using namespace abrviz;

class TestBundleLibrary : public QObject {
    Q_OBJECT

private slots:
    void parallelMapKeepsInputOrder()
    {
        QThreadPool pool;
        pool.setMaxThreadCount(4);
        QList<int> inputs;
        for (int i = 0; i < 200; ++i)
            inputs.append(i);
        auto future = parallelMap(inputs, [](const int& i) { return i * i; }, &pool);
        future.waitForFinished();
        QCOMPARE(future.resultCount(), 1);
        const QList<int> results = future.result();
        QCOMPARE(results.size(), 200);
        for (int i = 0; i < 200; ++i)
            QCOMPARE(results[i], i * i);
        QCOMPARE(future.progressValue(), 200);
    }

    void parallelMapOfNothing()
    {
        auto future = parallelMap(QList<int>{}, [](const int& i) { return i; });
        future.waitForFinished();
        QCOMPARE(future.resultCount(), 1);
        QVERIFY(future.result().isEmpty());
    }

    void parallelMapCancelled()
    {
        QThreadPool pool;
        pool.setMaxThreadCount(1);
        std::atomic<int> calls{0};
        std::atomic<bool> release{false};
        QList<int> inputs(50, 0);
        auto future = parallelMap(
            inputs,
            [&](const int&) {
                ++calls;
                while (!release.load())
                    std::this_thread::yield();
                return 0;
            },
            &pool);
        while (calls.load() == 0)
            std::this_thread::yield();
        future.cancel();
        release.store(true);
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
        QVERIFY(calls.load() < 50);  // the queued tasks saw the cancellation
    }

    void findsBundlesInFolder()
    {
        const QStringList bundles = findBundles(test::dataPath(QString()));
        QCOMPARE(bundles.size(), 2);  // `media` has no run.json
        QCOMPARE(QFileInfo(bundles[0]).fileName(), QStringLiteral("bbb_36"));
        QCOMPARE(QFileInfo(bundles[1]).fileName(), QStringLiteral("chef_cooking_18"));
    }

    void bundleFolderListsItself()
    {
        const QStringList bundles = findBundles(test::dataPath(QStringLiteral("bbb_36")));
        QCOMPARE(bundles.size(), 1);
        QCOMPARE(QFileInfo(bundles[0]).fileName(), QStringLiteral("bbb_36"));
    }

    void readsSummaries()
    {
        const BundleSummary chef = readBundleSummary(test::dataPath(QStringLiteral("chef_cooking_18")));
        QVERIFY2(chef.errors.isEmpty(), qPrintable(chef.errors.join(u'\n')));
        QCOMPARE(chef.runId, QStringLiteral("chef_cooking_llm_18Mbps_run_3"));
        QCOMPARE(chef.segments, 11);
        QCOMPARE(chef.stalls, 0);
        QVERIFY(!chef.cutOff);
        QCOMPARE(chef.linkRate.value_or(0), qint64(18'000'000));

        const BundleSummary bbb = readBundleSummary(test::dataPath(QStringLiteral("bbb_36")));
        QVERIFY2(bbb.errors.isEmpty(), qPrintable(bbb.errors.join(u'\n')));
        QCOMPARE(bbb.segments, 59);
        QCOMPARE(bbb.stalls, 1);
        QVERIFY(bbb.cutOff);
        QVERIFY(bbb.logDecisionAgreement < 1.0);
    }

    void missingBundleReportsErrors()
    {
        const BundleSummary missing = readBundleSummary(test::dataPath(QStringLiteral("nope")));
        QVERIFY(!missing.errors.isEmpty());
    }

    void scansWithTwoThreads()
    {
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        auto future = scanLibrary(test::dataPath(QString()), &pool);
        future.waitForFinished();
        QList<BundleSummaryPtr> bundles = future.result();
        QCOMPARE(bundles.size(), 2);
        sortLibrary(bundles);
        QCOMPARE(bundles[0]->videoName, QStringLiteral("big_buck_bunny"));
    }

    void cutOffEstimateMatchesFullLoad()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        auto future = scanLibrary(test::bundlesPath());
        future.waitForFinished();
        const QList<BundleSummaryPtr> bundles = future.result();
        QCOMPARE(bundles.size(), 14);
        int cutOff = 0;
        for (const BundleSummaryPtr& summary : bundles) {
            QVERIFY2(summary->errors.isEmpty(), qPrintable(summary->directory));
            const LoadResult loaded = loadBundleSync(summary->directory);
            QVERIFY(loaded.run);
            QVERIFY2(summary->cutOff == loaded.run->playbackCutOff(),
                     qPrintable(summary->runId));
            QCOMPARE(summary->stalls, static_cast<int>(loaded.run->stalls.size()));
            cutOff += summary->cutOff ? 1 : 0;
        }
        QCOMPARE(cutOff, 2);  // the two Big Buck Bunny runs
    }
};

QTEST_GUILESS_MAIN(TestBundleLibrary)
#include "tst_bundle_library.moc"
