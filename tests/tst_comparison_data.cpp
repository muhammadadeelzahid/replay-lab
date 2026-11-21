// Reading runs of several ABR algorithms: optional LLM files and fields, picture quality,
// rebuffering episodes and grouping into experiments.

#include "test_paths.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/metrics/highlights.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/model/run_data.h"

#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace abrviz;

namespace {

/// Copies a directory tree; returns false on the first failure.
bool copyTree(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(info.filePath()));
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return false;
        } else if (!QFile::copy(info.filePath(), target)) {
            return false;
        }
    }
    return true;
}

/// A summary with the fields grouping uses.
BundleSummaryPtr summary(const QString& video, qint64 rate, int run, const QString& algorithm)
{
    auto s = QSharedPointer<BundleSummary>::create();
    s->directory = QStringLiteral("/runs/%1_%2_%3_%4").arg(video, algorithm).arg(rate).arg(run);
    s->videoName = video;
    s->linkRate = rate;
    s->runNumber = run;
    s->algorithm = algorithm;
    return s;
}

/// Stalls of `length` seconds with `gap` seconds of playback between them, from `start`.
std::vector<Stall> stalls(double start, int count, double length, double gap)
{
    std::vector<Stall> list;
    double t = start;
    for (int i = 0; i < count; ++i) {
        list.push_back({t, t + length, false});
        t += length + gap;
    }
    return list;
}

} // namespace

class TestComparisonData : public QObject {
    Q_OBJECT

private slots:
    void namesAndOrderOfAlgorithms()
    {
        QCOMPARE(algorithmDisplayName(QStringLiteral("llm")), QStringLiteral("LLM"));
        QCOMPARE(algorithmDisplayName(QStringLiteral("RobustMPC")), QStringLiteral("RobustMPC"));
        QCOMPARE(algorithmDisplayName(QStringLiteral("mpc")), QStringLiteral("MPC"));
        QCOMPARE(algorithmDisplayName(QStringLiteral("bola")), QStringLiteral("BOLA"));
        QCOMPARE(algorithmDisplayName(QStringLiteral("festive")), QStringLiteral("festive"));
        QCOMPARE(algorithmDisplayName(QString()), QStringLiteral("ABR"));
        QVERIFY(algorithmRank(QStringLiteral("llm")) < algorithmRank(QStringLiteral("robustmpc")));
        QVERIFY(algorithmRank(QStringLiteral("robustmpc")) < algorithmRank(QStringLiteral("mpc")));
        QVERIFY(algorithmRank(QStringLiteral("mpc")) < algorithmRank(QStringLiteral("bola")));
        QVERIFY(algorithmRank(QStringLiteral("bola")) < algorithmRank(QStringLiteral("other")));
    }

    void stallsOneFrameApartAreOneEpisode()
    {
        // The ns-3 player resumes on one frame: pause, 33 ms of play, pause, ...
        auto list = stalls(5.4, 71, 0.1, 0.033);
        const auto episodes = stallEpisodes(list);
        QCOMPARE(episodes.size(), std::size_t{1});
        QCOMPARE(episodes.front().pauses, 71);
        QVERIFY(qAbs(episodes.front().stalledTime - 7.1) < 1e-9);
        QCOMPARE(episodes.front().start, 5.4);
        QCOMPARE(episodes.front().end, list.back().end);
    }

    void stallsFarApartStaySeparate()
    {
        auto list = stalls(10.0, 3, 0.5, 4.0);  // 4 s of playback between stalls
        const auto episodes = stallEpisodes(list);
        QCOMPARE(episodes.size(), std::size_t{3});
        for (const StallEpisode& e : episodes)
            QCOMPARE(e.pauses, 1);
        QVERIFY(stallEpisodes({}).empty());
        // Exactly the gap apart starts a new episode.
        QCOMPARE(stallEpisodes(stalls(0.0, 2, 0.2, kEpisodeGap)).size(), std::size_t{2});
    }

    void terminalStallMarksItsEpisode()
    {
        std::vector<Stall> list{{1.0, 1.2, false}, {1.25, 9.0, true}};
        const auto episodes = stallEpisodes(list);
        QCOMPARE(episodes.size(), std::size_t{1});
        QVERIFY(episodes.front().terminal);
    }

    void runWithoutPolicyLogLoadsCleanly()
    {
        QTemporaryDir dir;
        const QString bundle = dir.filePath(QStringLiteral("chef"));
        QVERIFY(copyTree(test::dataPath(QStringLiteral("chef_cooking_18")), bundle));
        QVERIFY(QFile::remove(QDir(bundle).filePath(QStringLiteral("logs/policy.log"))));
        const LoadResult result = loadBundleSync(bundle);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(u'\n')));
        QVERIFY(result.run);
        for (const Decision& d : result.run->decisions)
            QVERIFY(!d.llmRung);
    }

    void groupsByVideoRateAndRun()
    {
        const QList<BundleSummaryPtr> bundles{
            summary(QStringLiteral("chef"), 18'000'000, 3, QStringLiteral("bola")),
            summary(QStringLiteral("chef"), 18'000'000, 3, QStringLiteral("llm")),
            summary(QStringLiteral("bbb"), 9'000'000, 4, QStringLiteral("mpc")),
            summary(QStringLiteral("chef"), 18'000'000, 3, QStringLiteral("robustmpc")),
            summary(QStringLiteral("chef"), 9'000'000, 4, QStringLiteral("llm")),
            summary(QStringLiteral("chef"), 18'000'000, 3, QStringLiteral("mpc")),
        };
        const QList<Experiment> experiments = groupExperiments(bundles);
        QCOMPARE(experiments.size(), 3);
        // Videos in order of first appearance, then by rate.
        QCOMPARE(experiments[0].videoName, QStringLiteral("chef"));
        QCOMPARE(experiments[0].linkRate, std::optional<qint64>(9'000'000));
        QCOMPARE(experiments[1].linkRate, std::optional<qint64>(18'000'000));
        QCOMPARE(experiments[2].videoName, QStringLiteral("bbb"));
        QStringList order;
        for (const BundleSummaryPtr& run : experiments[1].runs)
            order << run->algorithm;
        QCOMPARE(order, (QStringList{QStringLiteral("llm"), QStringLiteral("robustmpc"),
                                     QStringLiteral("mpc"), QStringLiteral("bola")}));
    }

    void bundlesWithErrorsAreNotGrouped()
    {
        auto broken = QSharedPointer<BundleSummary>::create(*summary(QStringLiteral("chef"), 1, 1, QStringLiteral("llm")));
        broken->errors << QStringLiteral("run.json: cannot read");
        QVERIFY(groupExperiments({broken}).isEmpty());
    }

    // --- The delivered runs (skipped without viewer_bundles_phasef/) ------------------------------

    void deliveredRunsFormFifteenExperimentsOfFour()
    {
        if (!test::haveComparisonBundles())
            QSKIP("viewer_bundles_phasef/ is not present");
        QList<BundleSummaryPtr> bundles;
        for (const QString& dir : findBundles(test::comparisonBundlesPath()))
            bundles << QSharedPointer<const BundleSummary>::create(readBundleSummary(dir));
        QCOMPARE(bundles.size(), 60);
        for (const BundleSummaryPtr& b : bundles)
            QVERIFY2(b->errors.isEmpty(), qPrintable(b->directory + u": " + b->errors.join(u"; ")));
        const QList<Experiment> experiments = groupExperiments(bundles);
        QCOMPARE(experiments.size(), 15);
        for (const Experiment& e : experiments) {
            QCOMPARE(e.runs.size(), 4);
            QCOMPARE(e.runs.front()->algorithm, QStringLiteral("llm"));
            const int seed = e.runs.front()->seed;
            for (const BundleSummaryPtr& run : e.runs)
                QCOMPARE(run->seed, seed);
        }
    }

    void reportedStallRunsAreOneEpisodeEach()
    {
        if (!test::haveComparisonBundles())
            QSKIP("viewer_bundles_phasef/ is not present");
        // The three runs the ns-3 side checked by hand: one episode each.
        const QList<std::tuple<const char*, int, double>> cases{
            {"big_buck_bunny_robustmpc_18Mbps_run_3", 71, 7.65},
            {"chef_cooking_llm_9Mbps_run_4", 69, 9.12},
            {"pexels_34797065_bola_18Mbps_run_3", 35, 2.80},
        };
        for (const auto& [name, pauses, stalled] : cases) {
            const BundleSummary s = readBundleSummary(
                QDir(test::comparisonBundlesPath()).filePath(QString::fromLatin1(name)));
            QCOMPARE(s.stalls, pauses);
            QCOMPARE(s.stallEpisodes, 1);
            QVERIFY2(qAbs(s.stalledTime - stalled) < 0.01, qPrintable(QString::number(s.stalledTime)));
        }
    }

    void deliveredRunHasAlgorithmAndPictureQuality()
    {
        if (!test::haveComparisonBundles())
            QSKIP("viewer_bundles_phasef/ is not present");
        const LoadResult result = loadBundleSync(QDir(test::comparisonBundlesPath())
                                                     .filePath(QStringLiteral("chef_cooking_robustmpc_18Mbps_run_3")));
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(u'\n')));
        const RunData& run = *result.run;
        QCOMPARE(run.info.policyName, QStringLiteral("robustmpc"));
        QCOMPARE(run.info.policyFamily, QStringLiteral("mpc"));
        QVERIFY(!run.info.policyParams.isEmpty());
        QCOMPARE(run.info.policyParams.front().first, QStringLiteral("horizon"));
        QCOMPARE(run.info.policyParams.front().second, QStringLiteral("5"));
        // Every segment at every rung has scores, and the played ones are attached.
        QCOMPARE(run.ladderQuality.size(), std::size_t(run.video.segmentCount) * run.video.ladder.size());
        const SegmentQuality* q = run.qualityAt(0, 0);
        QVERIFY(q && q->vmaf && q->ssim && q->psnr);
        QVERIFY(qAbs(*q->vmaf - 28.7156) < 1e-6);
        QVERIFY(!run.qualityAt(0, 99));
        for (const Segment& s : run.segments)
            QVERIFY(s.quality.vmaf);

        const QoeReport qoe = computeQoe(run);
        QVERIFY(qoe.agreesWithSummary());
        QVERIFY(qoe.meanVmafShown && *qoe.meanVmafShown > 0.0 && *qoe.meanVmafShown <= 100.0);
        QVERIFY(qoe.minVmafShown && *qoe.minVmafShown <= *qoe.meanVmafShown);
        QVERIFY(!qoe.hasPolicyDetails);
    }

    void highlightsAreTheExperimentsWhereTheLlmLooksBetter()
    {
        if (!test::haveComparisonBundles())
            QSKIP("viewer_bundles_phasef/ is not present");
        const QList<BundleSummaryPtr> library = scanLibrary(test::comparisonBundlesPath()).result();
        std::vector<Highlight> found;
        for (const Experiment& e : groupExperiments(library)) {
            std::vector<std::shared_ptr<const RunData>> runs;
            for (const BundleSummaryPtr& r : e.runs)
                runs.push_back(loadBundleSync(r->directory).run);
            if (auto h = findHighlight(runs, QStringLiteral("llm")))
                found.push_back(*h);
        }
        std::ranges::sort(found, std::greater{}, &Highlight::score);
        QStringList names;
        for (const Highlight& h : found)
            names << QDir(h.directory).dirName();
        // LLM beats every other algorithm's mean VMAF by 2 or more without stalling longer.
        QCOMPARE(names, (QStringList{QStringLiteral("pexels_34797065_llm_18Mbps_run_3"),
                                     QStringLiteral("pexels_34797065_llm_9Mbps_run_4"),
                                     QStringLiteral("chef_cooking_llm_18Mbps_run_3"),
                                     QStringLiteral("peace_autumn_lake_llm_36Mbps_run_2"),
                                     QStringLiteral("big_buck_bunny_llm_36Mbps_run_2")}));
        for (const Highlight& h : found) {
            qInfo("%s: VMAF %.1f vs %.1f best other; segments %d-%d at %.1f s, %.0f vs %.0f for %s",
                  qPrintable(QDir(h.directory).dirName()), h.vmaf, h.bestOtherVmaf, h.segment,
                  h.segment + kHighlightSegments - 1, h.time, h.momentVmaf, h.rivalMomentVmaf,
                  qPrintable(h.rivalAlgorithm));
            QVERIFY(h.vmaf >= h.bestOtherVmaf + kHighlightMinGain);
            QVERIFY(h.momentVmaf > h.rivalMomentVmaf);
            QVERIFY(!h.rivalAlgorithm.isEmpty() && h.rivalAlgorithm != QStringLiteral("llm"));
            QVERIFY(h.time > 0.0);
        }
        // RobustMPC is not a winner for itself anywhere it stalled for 31 s.
        QVERIFY(!findHighlight({loadBundleSync(QDir(test::comparisonBundlesPath())
                                                   .filePath(QStringLiteral("pexels_34797065_robustmpc_9Mbps_run_4"))).run},
                               QStringLiteral("robustmpc")));
    }
};

QTEST_GUILESS_MAIN(TestComparisonData)
#include "tst_comparison_data.moc"
