#include "test_paths.h"

#include "chart_series.h"
#include "charts_panel.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/load/bundle_reader.h"

#include <QChart>
#include <QChartView>
#include <QGraphicsTextItem>
#include <QLineSeries>
#include <QSignalSpy>
#include <QTest>
#include <QValueAxis>

#include <cmath>

using namespace abrviz;

namespace {

using Chart = ChartsPanel::Chart;

std::shared_ptr<const RunData> load(const QString& directory)
{
    LoadResult result = loadBundleSync(directory);
    return result.run;
}

struct Item {
    double t;
    std::optional<double> v;
};

/// A panel on its own, shown at a fixed size so the plot areas have their final geometry.
struct Fixture {
    Session session;
    ChartsPanel panel{&session};

    explicit Fixture(const QString& bundle)
    {
        session.setRun(load(bundle));
        panel.resize(900, 900);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
    }
};

QList<QPointF> pointsOf(QChart* chart, const QString& name)
{
    QList<QPointF> points;
    for (QAbstractSeries* series : chart->series()) {
        if (series->name() == name)
            points += static_cast<QXYSeries*>(series)->points();
    }
    return points;
}

} // namespace

class TestChartsPanel : public QObject {
    Q_OBJECT

private slots:
    void contiguousRunsSplitAtMissingValues()
    {
        const std::vector<Item> items = {{0, std::nullopt}, {1, 1.0}, {2, std::nullopt},
                                         {3, 2.0},          {4, 3.0}, {5, std::nullopt}};
        const auto runs = contiguousRuns(items, &Item::t, &Item::v, 10.0);
        QCOMPARE(runs.size(), std::size_t(2));
        QCOMPARE(runs[0], (QList<QPointF>{{1, 10}}));
        QCOMPARE(runs[1], (QList<QPointF>{{3, 20}, {4, 30}}));

        const std::vector<Item> none = {{0, std::nullopt}};
        QVERIFY(contiguousRuns(none, &Item::t, &Item::v).empty());
    }

    void stepRunsBreakBetweenIntervals()
    {
        const auto runs = stepRuns({{0, 1, 5}, {1, 2, 6}, {3, 4, 7}}, 0.05);
        QCOMPARE(runs.size(), std::size_t(2));
        QCOMPARE(runs[0], (QList<QPointF>{{0, 5}, {1, 5}, {1, 6}, {2, 6}}));
        QCOMPARE(runs[1], (QList<QPointF>{{3, 7}, {4, 7}}));
    }

    void missingRttIsAGap()
    {
        Fixture f(test::dataPath(QStringLiteral("chef_cooking_18")));
        const auto& samples = f.session.run()->samples;
        QVERIFY(!samples.front().rttMs);  // before the first measurement
        QCOMPARE(f.panel.seriesCount(Chart::RttLoss, QStringLiteral("rtt")), 1);
        const QList<QPointF> rtt = pointsOf(f.panel.chart(Chart::RttLoss), QStringLiteral("rtt"));
        QCOMPARE(rtt.size(), static_cast<qsizetype>(samples.size() - 1));
        QCOMPARE(rtt.first().x(), samples[1].time);
        for (const QPointF& p : rtt)
            QVERIFY(p.y() > 0.0);
        // Loss was recorded from the start.
        QCOMPARE(pointsOf(f.panel.chart(Chart::RttLoss), QStringLiteral("loss")).size(),
                 static_cast<qsizetype>(samples.size()));
    }

    void missingThroughputIsAGap()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        Fixture f(test::bundlesPath() + QStringLiteral("/pexels_34797065_llm_36Mbps_run_2"));
        // The estimate is missing for the last 9 samples, after the last download: the line
        // stops at the last value instead of dropping to 0.
        const auto& samples = f.session.run()->samples;
        const auto last = std::find_if(samples.rbegin(), samples.rend(), [](const Sample& s) {
            return s.throughput.has_value();
        });
        QVERIFY(last != samples.rend());
        QVERIFY(last != samples.rbegin());  // values are missing after it
        const QList<QPointF> estimate =
            pointsOf(f.panel.chart(Chart::Throughput), QStringLiteral("estimate"));
        const auto present = std::ranges::count_if(
            samples, [](const Sample& s) { return s.throughput.has_value(); });
        QCOMPARE(estimate.size(), static_cast<qsizetype>(present));
        QCOMPARE(estimate.last().x(), last->time);
        for (const QPointF& p : estimate)
            QVERIFY(p.y() > 0.0);
    }

    void chartsHaveTheirData()
    {
        Fixture f(test::dataPath(QStringLiteral("bbb_36")));
        const RunData& run = *f.session.run();
        QCOMPARE(pointsOf(f.panel.chart(Chart::Buffer), QStringLiteral("buffer")).size(),
                 static_cast<qsizetype>(run.buffer.size()));
        QCOMPARE(f.panel.seriesCount(Chart::Buffer, QStringLiteral("stall")), 1);
        QCOMPARE(pointsOf(f.panel.chart(Chart::Bitrate), QStringLiteral("override")).size(), 3);
        // The stall splits the line of bitrates on screen.
        QCOMPARE(f.panel.seriesCount(Chart::Bitrate, QStringLiteral("played")), 2);
        QVERIFY(f.panel.seriesCount(Chart::Throughput, QStringLiteral("measured")) > 0);
        // A few downloads are logged as far faster than the 36 Mbps link; the axis stays at
        // the scale of the link and the estimate.
        const auto measured = pointsOf(f.panel.chart(Chart::Throughput), QStringLiteral("measured"));
        QVERIFY(std::ranges::any_of(measured, [](const QPointF& p) { return p.y() > 100.0; }));
        const auto* axis = static_cast<QValueAxis*>(
            f.panel.chart(Chart::Throughput)->axes(Qt::Vertical).first());
        QVERIFY2(axis->max() <= 36.0 * 1.1 * 1.1 + 1e-9, qPrintable(QString::number(axis->max())));
        QVERIFY(!pointsOf(f.panel.chart(Chart::DecisionDelay), QStringLiteral("delay")).isEmpty());
    }

    void allChartsFollowTheCursor()
    {
        Fixture f(test::dataPath(QStringLiteral("bbb_36")));
        for (const double t : {10.0, 57.0, 120.0, 0.0, f.session.endTime()}) {
            f.session.cursor()->setTime(t, TimeCursor::Source::Timeline);
            const double first = f.panel.cursorX(Chart::Buffer);
            for (int i = 0; i < ChartsPanel::kChartCount; ++i) {
                const auto chart = static_cast<Chart>(i);
                QCOMPARE(f.panel.cursorX(chart), f.panel.xAt(chart, f.session.cursor()->time()));
                QCOMPARE(f.panel.cursorX(chart), first);  // one shared x-axis
            }
        }
    }

    void resizingKeepsTheAxisInView()
    {
        Fixture f(test::dataPath(QStringLiteral("bbb_36")));
        f.session.cursor()->setTime(f.session.endTime());
        for (const int width : {600, 1200, 500}) {
            f.panel.resize(width, 800);
            QTest::qWait(50);
            const auto views = f.panel.findChildren<QChartView*>();
            QCOMPARE(views.size(), ChartsPanel::kChartCount);
            for (int i = 0; i < ChartsPanel::kChartCount; ++i) {
                const auto chart = static_cast<Chart>(i);
                const double right = views[i]->chart()->plotArea().right();
                QVERIFY2(right <= views[i]->viewport()->width(), qPrintable(QString::number(width)));
                QVERIFY(std::abs(f.panel.cursorX(chart) - right) < 0.5);  // end of the axis
                QVERIFY(right > views[i]->viewport()->width() - 60);       // and uses the width
            }
            // The x-axis labels (drawn only under the last chart) moved with the plot area.
            QChart* last = views.back()->chart();
            double rightmostLabel = 0.0;
            for (QGraphicsItem* item : last->scene()->items()) {
                if (auto* text = dynamic_cast<QGraphicsTextItem*>(item);
                    text && text->isVisible() && text->toPlainText().endsWith(QStringLiteral(" s")))
                    rightmostLabel = std::max(rightmostLabel, text->sceneBoundingRect().center().x());
            }
            QVERIFY(rightmostLabel > 0.0);
            const double plotRight = last->mapToScene(last->plotArea().topRight()).x();
            QVERIFY2(std::abs(rightmostLabel - plotRight) < 80.0,
                     qPrintable(QStringLiteral("label at %1, plot ends at %2")
                                    .arg(rightmostLabel)
                                    .arg(plotRight)));
        }
    }

    void readoutFollowsTheCursor()
    {
        Fixture f(test::dataPath(QStringLiteral("bbb_36")));
        f.session.cursor()->setTime(1.0);  // first sample: no RTT yet
        QVERIFY2(f.panel.readout().contains(QStringLiteral("RTT -")), qPrintable(f.panel.readout()));
        f.session.cursor()->setTime(57.0);  // inside the stall
        QVERIFY2(f.panel.readout().contains(QStringLiteral("stalled")), qPrintable(f.panel.readout()));
        f.session.cursor()->setTime(100.0);
        QVERIFY2(f.panel.readout().contains(QStringLiteral("segment")), qPrintable(f.panel.readout()));
        QVERIFY(f.panel.readout().contains(QStringLiteral(" ms")));
    }

    void clickingAChartMovesTheCursor()
    {
        Fixture f(test::dataPath(QStringLiteral("chef_cooking_18")));
        QSignalSpy changed(f.session.cursor(), &TimeCursor::timeChanged);
        auto* view = f.panel.findChildren<QChartView*>().at(2);
        const QPointF inChart(f.panel.xAt(Chart::Bitrate, 20.0), 20.0);
        const QPoint pos = view->mapFromScene(view->chart()->mapToScene(inChart));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, pos);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Charts);
        QVERIFY(std::abs(f.session.cursor()->time() - 20.0) < 0.5);
    }

    void closingTheRunHidesTheCursor()
    {
        Fixture f(test::dataPath(QStringLiteral("chef_cooking_18")));
        QVERIFY(!std::isnan(f.panel.cursorX(Chart::Buffer)));
        f.session.clear();
        for (int i = 0; i < ChartsPanel::kChartCount; ++i) {
            QVERIFY(std::isnan(f.panel.cursorX(static_cast<Chart>(i))));
            QVERIFY(f.panel.chart(static_cast<Chart>(i))->series().isEmpty());
        }
        QCOMPARE(f.panel.readout(), QStringLiteral("No run open."));
    }
};

QTEST_MAIN(TestChartsPanel)
#include "tst_charts_panel.moc"
