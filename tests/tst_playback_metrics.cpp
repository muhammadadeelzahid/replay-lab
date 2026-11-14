#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/playback/playback_clock.h"
#include "test_paths.h"

#include <QTest>

using namespace abrviz;

namespace {

/// Three segments on screen at 1.0, 4.3 and 8.0 s, with a stall from 7.6 to 8.0 s.
/// Media durations 2, 2 and 1 s; the last segment ends at 11.267 + 0.033 s.
RunData makeRun()
{
    RunData run;
    run.video.segmentCount = 3;
    run.video.ladder = {{0, 250000, 428, 240}, {1, 750000, 640, 360}, {2, 1500000, 854, 480}};
    run.streamStart = 0.1;
    run.playbackStart = 1.0;
    run.playbackEnd = 11.3;
    for (int i = 0; i < 3; ++i) {
        Segment s;
        s.index = i;
        s.playedRung = i == 1 ? 2 : 1;
        s.mediaDuration = i == 2 ? 1.0 : 2.0;
        run.segments.push_back(s);
    }
    run.playback = {{0, 1.0, 4.267, 100, 750000},
                    {1, 4.3, 7.567, 100, 1500000},
                    {2, 8.0, 11.267, 100, 750000}};
    run.stalls = {{7.6, 8.0, false}};
    return run;
}

} // namespace

class TestPlaybackMetrics : public QObject {
    Q_OBJECT

private slots:
    void mapsSegmentsAndStalls()
    {
        const PlaybackClock clock(makeRun());
        QCOMPARE(clock.pieces().size(), 4u);
        QCOMPARE(clock.mediaDuration(), 5.0);
        QVERIFY(qFuzzyCompare(clock.replayDuration(), 5.4));

        QCOMPARE(clock.simToReplay(0.5), 0.0);  // before playback
        QCOMPARE(clock.simToReplay(1.0), 0.0);
        QCOMPARE(clock.simToReplay(4.3), 2.0);
        QVERIFY(qFuzzyCompare(clock.simToReplay(5.95), 3.0));  // half way through segment 1
        QVERIFY(qFuzzyCompare(clock.simToReplay(7.8), 4.2));   // half way through the stall
        QVERIFY(qFuzzyCompare(clock.simToReplay(8.0), 4.4));
        QVERIFY(qFuzzyCompare(clock.simToReplay(100.0), 5.4));  // clamped at the end

        // The media timeline does not advance during the stall.
        QVERIFY(qFuzzyCompare(clock.replayToMedia(4.2), 4.0));
        QVERIFY(qFuzzyCompare(clock.replayToMedia(4.9), 4.5));
        // At the stall's media time, the segment after the stall is preferred.
        QVERIFY(qFuzzyCompare(clock.mediaToReplay(4.0), 4.4));
    }

    void positions()
    {
        const PlaybackClock clock(makeRun());
        QVERIFY(clock.positionAtSim(0.5).beforeStart);

        const auto middle = clock.positionAtSim(5.95);
        QCOMPARE(middle.segment, 1);
        QVERIFY(qFuzzyCompare(middle.fraction, 0.5));
        QVERIFY(!middle.stalled);

        const auto stalled = clock.positionAtSim(7.7);
        QVERIFY(stalled.stalled);
        QCOMPARE(stalled.segment, 1);  // the last segment shown stays on screen

        const auto end = clock.positionAtSim(50.0);
        QVERIFY(end.afterEnd);
        QCOMPARE(end.segment, 2);

        const auto replay = clock.positionAtReplay(4.6);
        QCOMPARE(replay.segment, 2);
        QVERIFY(qFuzzyCompare(replay.fraction, 0.2));
    }

    void roundTrips()
    {
        const PlaybackClock clock(makeRun());
        double previous = -1.0;
        for (double t = clock.simStart(); t < clock.simEnd(); t += 0.01) {
            const double replay = clock.simToReplay(t);
            QVERIFY(replay >= previous);  // never goes backwards
            previous = replay;
            QVERIFY2(qAbs(clock.replayToSim(replay) - t) < 1e-9, qPrintable(QString::number(t)));
        }
    }

    void cutOffAndSkippedSegments()
    {
        RunData run = makeRun();
        run.video.segmentCount = 4;            // segment 3 was never shown
        run.playback.back().framesPlayed = 50;  // and segment 2 stopped half way
        run.segments.push_back({.index = 3, .mediaDuration = 2.0, .played = false});
        QVERIFY(run.playbackCutOff());
        const PlaybackClock cut(run);
        QCOMPARE(cut.mediaDuration(), 4.5);

        RunData skipped = makeRun();
        skipped.playback.erase(skipped.playback.begin() + 1);  // segment 1 never shown
        skipped.stalls.clear();
        const PlaybackClock clock(skipped);
        QCOMPARE(clock.pieces().size(), 2u);
        QCOMPARE(clock.mediaDuration(), 3.0);
        QCOMPARE(clock.positionAtSim(5.0).segment, 0);
    }

    void qoeFromSyntheticRun()
    {
        RunData run = makeRun();
        run.decisions = {
            {.record = 0, .forSegment = 1, .time = 0.5, .chosenRung = 2, .decisionDelay = 0.2,
             .guardOverride = true},
            {.record = 1, .forSegment = 2, .time = 1.0, .chosenRung = 1, .decisionDelay = 0.03},
            {.record = 2, .forSegment = 3, .time = 2.0, .chosenRung = 1, .used = false},
        };
        run.samples = {{.time = 0.5, .throughput = 1e6},
                       {.time = 1.0, .throughput = 3e6},
                       {.time = 1.5}};

        const QoeReport r = computeQoe(run);
        QCOMPARE(r.segmentsShown, 3);
        QVERIFY(qFuzzyCompare(r.averagePlayedBitrate, (750000.0 + 1500000.0 + 750000.0) / 3));
        QCOMPARE(r.playedSwitches, 2);
        QVERIFY(qFuzzyCompare(r.averageChosenBitrate, (1500000.0 + 750000.0 + 750000.0) / 3));
        QCOMPARE(r.chosenSwitches, 1);
        QVERIFY(qFuzzyCompare(r.startupDelay, 0.9));
        QCOMPARE(r.stallCount, 1);
        QVERIFY(qFuzzyCompare(r.rebufferTime, 0.4));
        QCOMPARE(r.averageThroughput, 2e6);  // the missing value is skipped, not counted as 0
        QCOMPARE(r.decisions, 2);
        QCOMPARE(r.guardOverrides, 1);
        QVERIFY(qFuzzyCompare(r.averageDecisionDelay, 0.115));
        QCOMPARE(r.maxDecisionDelay, 0.2);
    }

    void summaryChecks()
    {
        SummaryCheck close{QStringLiteral("x"), 100.5, 100.0};
        QVERIFY(close.agrees());
        SummaryCheck far{QStringLiteral("x"), 102.0, 100.0};
        QVERIFY(!far.agrees());
        SummaryCheck zero{QStringLiteral("x"), 0.0005, 0.0};
        QVERIFY(zero.agrees());
        SummaryCheck missing{QStringLiteral("x"), 5.0, std::nullopt};
        QVERIFY(missing.agrees());
    }

    void fixtures()
    {
        const auto bbb = loadBundleSync(test::dataPath(QStringLiteral("bbb_36")));
        QVERIFY(bbb.run);
        const PlaybackClock clock(*bbb.run);
        QVERIFY(clock.positionAtSim(57.0).stalled);
        // The test data has no media, so every segment uses the nominal 2 s:
        // 54 whole segments, 39 % of segment 54, and the 0.3607 s stall.
        QVERIFY(qAbs(clock.replayDuration() - (54 * 2.0 + 0.39 * 2.0 + 0.3607)) < 1e-6);

        for (const char* name : {"bbb_36", "chef_cooking_18"}) {
            const auto result = loadBundleSync(test::dataPath(QLatin1StringView(name)));
            const QoeReport report = computeQoe(*result.run);
            QVERIFY2(report.agreesWithSummary(), name);
        }
    }

    void allDeliveredBundlesAgreeWithSummary()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ is not present");
        const QDir root(test::bundlesPath());
        for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto result = loadBundleSync(root.filePath(name));
            const QoeReport report = computeQoe(*result.run);
            QVERIFY2(report.agreesWithSummary(), qPrintable(name));
            const PlaybackClock clock(*result.run);
            QVERIFY(clock.replayDuration() > 0.0);
            QVERIFY(clock.replayDuration() < clock.simEnd() - clock.simStart());
        }
    }
};

QTEST_GUILESS_MAIN(TestPlaybackMetrics)
#include "tst_playback_metrics.moc"
