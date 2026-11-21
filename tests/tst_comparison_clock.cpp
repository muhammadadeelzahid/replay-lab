// ComparisonClock: one master time for several runs, by session time or by media time.
//
// The synthetic run (tests/synthetic_run.h): stream start 0.5 s, first frame 1.0 s, three
// 2.5 s segments, a stall of `stall` seconds from 7.6 s, and playback cut off half way
// through the third segment. Media time 0 to 6.25 s.

#include "synthetic_run.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/playback/comparison_clock.h"

#include <QDir>

#include <QtTest>

#include <cmath>
#include <limits>

using namespace abrviz;
using Alignment = ComparisonClock::Alignment;

namespace {

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

ComparisonClock twoRuns(Alignment alignment)
{
    // Run 0 stalls for 1 s, run 1 for 3 s.
    return ComparisonClock({test::syntheticRun(1.0, QStringLiteral("a")),
                            test::syntheticRun(3.0, QStringLiteral("b"))},
                           alignment);
}

} // namespace

class TestComparisonClock : public QObject {
    Q_OBJECT

private slots:
    void sessionTimeLinesUpTheSameMoment()
    {
        const ComparisonClock clock = twoRuns(Alignment::SessionTime);
        QCOMPARE(clock.runCount(), 2);
        // The master timeline is the longer session.
        const double longer = clock.clock(1).simEnd() - clock.run(1)->streamStart;
        QVERIFY(near(clock.duration(), longer));

        // Before the first frame both are starting.
        const auto start = clock.positionOf(0, 0.0);
        QVERIFY(start.starting);
        QVERIFY(near(start.sim, 0.5));
        QVERIFY(near(start.replay, 0.0));

        // 7.5 s in (simulation 8.0 s): both stalled.
        QVERIFY(clock.positionOf(0, 7.5).position.stalled);
        QVERIFY(clock.positionOf(1, 7.5).position.stalled);
        // 8.5 s in (simulation 9.0 s): the 1 s stall is over, the 3 s one is not.
        const auto a = clock.positionOf(0, 8.5);
        const auto b = clock.positionOf(1, 8.5);
        QVERIFY(!a.position.stalled);
        QCOMPARE(a.position.segment, 2);
        QVERIFY(b.position.stalled);
        QCOMPARE(b.position.segment, 1);
        // 10.5 s in (simulation 11.0 s): run 0 was cut off at 10.27 s, run 1 still plays.
        QVERIFY(clock.positionOf(0, 10.5).ended);
        QVERIFY(!clock.positionOf(1, 10.5).ended);
        QCOMPARE(clock.positionOf(1, 10.5).position.segment, 2);
    }

    void mediaTimeLinesUpTheSameFrame()
    {
        const ComparisonClock clock = twoRuns(Alignment::MediaTime);
        QVERIFY(near(clock.duration(), 6.25));
        // In the second segment both show the same frame at the same replay time.
        for (int run = 0; run < 2; ++run) {
            const auto p = clock.positionOf(run, 3.0);
            QCOMPARE(p.position.segment, 1);
            QVERIFY(near(p.position.fraction, 0.2));
            QVERIFY(near(p.replay, 3.0));
        }
        // After the stall the same frame is later in the replay of the run that stalled longer.
        const auto a = clock.positionOf(0, 6.0);
        const auto b = clock.positionOf(1, 6.0);
        QCOMPARE(a.position.segment, 2);
        QCOMPARE(b.position.segment, 2);
        QVERIFY(near(a.replay, 7.0));
        QVERIFY(near(b.replay, 9.0));
        QVERIFY(!a.position.stalled);
    }

    void masterOfIsTheInverse()
    {
        for (const Alignment alignment : {Alignment::SessionTime, Alignment::MediaTime}) {
            const ComparisonClock clock = twoRuns(alignment);
            for (int run = 0; run < 2; ++run) {
                // Times inside segments map back exactly (stalls collapse in media time).
                for (const double t : {1.0, 2.2, 3.9, 5.5}) {
                    const auto p = clock.positionOf(run, t);
                    if (p.position.stalled || p.starting || p.ended)
                        continue;
                    QVERIFY2(near(clock.masterOf(run, p.replay), t, 1e-6),
                             qPrintable(QStringLiteral("run %1 at %2").arg(run).arg(t)));
                }
            }
        }
        // In media time, a replay time inside a stall is the stall's media time.
        const ComparisonClock media = twoRuns(Alignment::MediaTime);
        QVERIFY(near(media.masterOf(1, 6.0), 5.0));
    }

    void outOfRangeAndOddInputs()
    {
        const ComparisonClock clock = twoRuns(Alignment::SessionTime);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        QVERIFY(near(clock.positionOf(0, nan).sim, 0.5));
        QVERIFY(near(clock.positionOf(0, -5.0).sim, 0.5));
        QVERIFY(clock.positionOf(1, inf).ended);
        QCOMPARE(clock.positionOf(7, 1.0).replay, 0.0);
        QCOMPARE(clock.masterOf(-1, 1.0), 0.0);
        QVERIFY(!clock.run(5));
        QCOMPARE(clock.clock(5).replayDuration(), 0.0);

        const ComparisonClock empty;
        QCOMPARE(empty.runCount(), 0);
        QCOMPARE(empty.duration(), 0.0);

        // A null run is kept as an empty pane and does not affect the others.
        const ComparisonClock withEmpty({test::syntheticRun(1.0), nullptr}, Alignment::MediaTime);
        QCOMPARE(withEmpty.runCount(), 2);
        QVERIFY(near(withEmpty.duration(), 6.25));
        QCOMPARE(withEmpty.positionOf(1, 3.0).replay, 0.0);
    }


    void deliveredExperimentSweepsCleanly()
    {
        if (!test::haveComparisonBundles())
            QSKIP("viewer_bundles_phasef/ is not present");
        std::vector<std::shared_ptr<const RunData>> runs;
        for (const char* algorithm : {"llm", "robustmpc", "mpc", "bola"}) {
            const LoadResult result = loadBundleSync(QDir(test::comparisonBundlesPath())
                                                         .filePath(QStringLiteral("chef_cooking_%1_9Mbps_run_4")
                                                                       .arg(QLatin1String(algorithm))));
            QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(u'\n')));
            runs.push_back(result.run);
        }
        for (const Alignment alignment : {Alignment::SessionTime, Alignment::MediaTime}) {
            const ComparisonClock clock(runs, alignment);
            QVERIFY(clock.duration() > 0.0);
            // Every run's replay time moves forward (or holds) as the master time moves on,
            // and every run has ended at the end of the master timeline.
            std::vector<double> last(runs.size(), -1.0);
            for (double t = 0.0; t <= clock.duration(); t += 0.05) {
                for (int run = 0; run < clock.runCount(); ++run) {
                    const auto p = clock.positionOf(run, t);
                    QVERIFY(std::isfinite(p.replay));
                    QVERIFY2(p.replay >= last[std::size_t(run)] - 1e-9,
                             qPrintable(QStringLiteral("run %1 at %2").arg(run).arg(t)));
                    last[std::size_t(run)] = p.replay;
                }
            }
            for (int run = 0; run < clock.runCount(); ++run)
                QVERIFY(clock.positionOf(run, clock.duration()).ended);
        }
    }
};

QTEST_GUILESS_MAIN(TestComparisonClock)
#include "tst_comparison_clock.moc"
