#include "test_paths.h"

#include "library_view.h"
#include "main_window.h"
#include "replay_player.h"
#include "moment_panel.h"
#include "report_view.h"
#include "transport_bar.h"
#include "segment_timeline.h"
#include "session.h"
#include "summary_panel.h"
#include "time_cursor.h"

#include "abrviz/core/version.h"

#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>

#include <memory>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 20000;

QString chef() { return test::dataPath(QStringLiteral("chef_cooking_18")); }
QString bbb() { return test::dataPath(QStringLiteral("bbb_36")); }

QStringList menuTexts(QMenu* menu)
{
    QStringList texts;
    for (QAction* action : menu->actions()) {
        if (!action->isSeparator())
            texts.append(action->text());
    }
    return texts;
}

/// True once `spy` has a signal, including one that arrived before `QSignalSpy::wait()` was
/// called (which `wait()` alone would miss).
bool waitFor(QSignalSpy& spy)
{
    return !spy.isEmpty() || spy.wait(kTimeoutMs);
}

/// Opens `bundle` and waits for the load to finish.
bool open(MainWindow& window, const QString& bundle)
{
    QSignalSpy opened(&window, &MainWindow::bundleOpened);
    window.openBundle(bundle);
    return waitFor(opened) && opened.first().at(1).toBool();
}

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void showsTitleAndVersion()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.windowTitle(), QStringLiteral("abr-viz"));

        bool found = false;
        for (QLabel* label : window.statusBar()->findChildren<QLabel*>())
            found = found || label->text().contains(versionString());
        QVERIFY(found);
    }

    void hasMenus()
    {
        MainWindow window;
        const auto menus = window.menuBar()->actions();
        QCOMPARE(menus.size(), 4);
        QCOMPARE(menus[0]->text(), QStringLiteral("&File"));
        QCOMPARE(menuTexts(menus[0]->menu()),
                 (QStringList{QStringLiteral("Open &Folder..."), QStringLiteral("Open &Bundle..."),
                              QStringLiteral("Open &Recent"), QStringLiteral("&Quit")}));
        QCOMPARE(menus[1]->text(), QStringLiteral("&View"));
        QCOMPARE(menuTexts(menus[1]->menu()),
                 (QStringList{QStringLiteral("Library"), QStringLiteral("Replay"),
                              QStringLiteral("Analysis"), QStringLiteral("Logs"),
                              QStringLiteral("Report"), QStringLiteral("Show Video &Overlay"),
                              QStringLiteral("Enter &Full Screen")}));
        QCOMPARE(menus[2]->text(), QStringLiteral("&Playback"));
        QCOMPARE(menuTexts(menus[2]->menu()),
                 (QStringList{QStringLiteral("&Play"), QStringLiteral("&Back 5 Seconds"),
                              QStringLiteral("&Forward 5 Seconds"), QStringLiteral("P&revious Segment"),
                              QStringLiteral("&Next Segment"), QStringLiteral("&Render Video"),
                              QStringLiteral("&Engine")}));
        QMenu* engines = menus[2]->menu()->actions().last()->menu();
        QVERIFY(engines);
        QCOMPARE(engines->actions().size(), 2);
        int checked = 0;
        for (QAction* action : engines->actions())
            checked += action->isChecked() ? 1 : 0;
        QCOMPARE(checked, 1);
        QCOMPARE(menus[3]->text(), QStringLiteral("&Help"));
    }

    void pagesFollowTheRun()
    {
        MainWindow window;
        QCOMPARE(window.currentPage(), MainWindow::Page::Library);
        QVERIFY(window.library()->isShowingWelcome());
        QVERIFY(!window.transport()->isVisibleTo(&window));
        QVERIFY(open(window, test::dataPath(QStringLiteral("bbb_36"))));
        // Opening a run from the library shows its replay, with the transport bar.
        QCOMPARE(window.currentPage(), MainWindow::Page::Replay);
        QVERIFY(window.transport()->isVisibleTo(&window));
        for (int i = 0; i < MainWindow::kPageCount; ++i) {
            window.showPage(static_cast<MainWindow::Page>(i));
            QCOMPARE(window.currentPage(), static_cast<MainWindow::Page>(i));
        }
    }

    void transportStepsTheCursor()
    {
        MainWindow window;
        QVERIFY(open(window, test::dataPath(QStringLiteral("bbb_36"))));
        Session* session = window.session();
        const PlaybackClock& clock = session->clock();
        session->cursor()->setTime(clock.replayToSim(20.0));
        window.stepReplay(5.0);
        QVERIFY(std::abs(clock.simToReplay(session->cursor()->time()) - 25.0) < 0.01);
        window.stepReplay(-10.0);
        QVERIFY(std::abs(clock.simToReplay(session->cursor()->time()) - 15.0) < 0.01);

        // Segment steps land on the first frame of a segment.
        window.stepSegment(1);
        const double next = session->cursor()->time();
        const auto starts = [&clock] {
            std::vector<double> times;
            for (const auto& piece : clock.pieces())
                if (piece.kind == PlaybackClock::Piece::Kind::Segment)
                    times.push_back(piece.simStart);
            return times;
        }();
        QVERIFY(std::ranges::find(starts, next) != starts.end());
        window.stepSegment(-1);
        QVERIFY(session->cursor()->time() < next);
        QVERIFY(window.transport()->timeText().contains(QStringLiteral("replay")));
        QVERIFY(!window.moment()->value(QStringLiteral("Segment")).isEmpty());
    }

    void libraryFiltersRuns()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        MainWindow window;
        QSignalSpy scanned(&window, &MainWindow::folderScanned);
        window.openFolder(test::bundlesPath());
        QVERIFY(waitFor(scanned));
        QVERIFY(!window.library()->isShowingWelcome());
        QCOMPARE(window.library()->visibleRows(), 14);
        window.library()->searchEdit()->setText(QStringLiteral("bunny"));
        QCOMPARE(window.library()->visibleRows(), 2);
        QVERIFY(window.library()->detailsText().contains(QStringLiteral("Big Buck Bunny")));
        window.library()->searchEdit()->clear();
        QCOMPARE(window.library()->visibleRows(), 14);
    }

    void engineCanBeSwitched()
    {
        MainWindow window;
        QVERIFY(window.setEngine(Engine::Prerendered));
        QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::Prerendered));
#if ABRVIZ_WITH_GSTREAMER
        QVERIFY(window.setEngine(Engine::GStreamer));
        QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::GStreamer));
#else
        QVERIFY(!window.setEngine(Engine::GStreamer));
        QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::Prerendered));
#endif
    }

#if ABRVIZ_WITH_GSTREAMER
    void gstreamerFailureFallsBackToPrerendered()
    {
        // The test bundles carry no media files, so GStreamer cannot show them.
        MainWindow window;
        QVERIFY(window.setEngine(Engine::GStreamer));
        QSignalSpy fellBack(window.player(), &ReplayPlayer::engineFellBack);
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(test::dataPath(QStringLiteral("chef_cooking_18")));
        QVERIFY(waitFor(opened));
        QTRY_COMPARE(fellBack.size(), 1);
        QVERIFY(fellBack.first().first().toString().contains(QStringLiteral("missing")));
        QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::Prerendered));
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("pre-rendered")));
    }
#endif

    void timeCursorClampsAndSkipsNoOps()
    {
        TimeCursor cursor;
        QSignalSpy changed(&cursor, &TimeCursor::timeChanged);
        cursor.setRange(10.0, 20.0);
        QCOMPARE(cursor.time(), 10.0);
        cursor.setTime(25.0, TimeCursor::Source::Timeline);
        QCOMPARE(cursor.time(), 20.0);
        QCOMPARE(changed.last().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Timeline);
        const auto count = changed.size();
        cursor.setTime(20.0);
        QCOMPARE(changed.size(), count);
    }

    void scansFolderIntoLibrary()
    {
        MainWindow window;
        QSignalSpy scanned(&window, &MainWindow::folderScanned);
        window.openFolder(test::dataPath(QString()));
        QVERIFY(waitFor(scanned));
        LibraryModel* model = window.library()->model();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->bundleAt(0)->videoName, QStringLiteral("big_buck_bunny"));
        QCOMPARE(model->data(model->index(0, LibraryModel::CutOff), Qt::DisplayRole).toString(),
                 QStringLiteral("cut off"));
        QCOMPARE(model->data(model->index(1, LibraryModel::CutOff), Qt::DisplayRole).toString(),
                 QStringLiteral("complete"));
        QVERIFY(!window.isBusy());
    }

    void openingFromLibraryFillsPanels()
    {
        MainWindow window;
        window.show();
        QSignalSpy scanned(&window, &MainWindow::folderScanned);
        window.openFolder(test::dataPath(QString()));
        QVERIFY(waitFor(scanned));

        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        emit window.library()->openRequested(window.library()->model()->bundleAt(1)->directory);
        QVERIFY(waitFor(opened));
        QVERIFY(opened.first().at(1).toBool());

        const auto& run = window.session()->run();
        QVERIFY(run);
        QCOMPARE(run->info.runId, QStringLiteral("chef_cooking_llm_18Mbps_run_3"));
        QVERIFY(window.windowTitle().startsWith(run->info.runId));
        QVERIFY(window.summary()->toPlainText().contains(run->info.runId));
        QCOMPARE(window.session()->cursor()->time(), run->playbackStart);

        SegmentTimeline* timeline = window.timeline();
        QCOMPARE(timeline->count(SegmentTimeline::Kind::Download), 11);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::Segment), 11);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::GuardOverride), 2);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::Stall), 0);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::CutOff), 0);
    }

    void timelineShowsStallAndCutOff()
    {
        MainWindow window;
        QVERIFY(open(window, bbb()));
        SegmentTimeline* timeline = window.timeline();
        QCOMPARE(timeline->count(SegmentTimeline::Kind::Stall), 1);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::CutOff), 1);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::GuardOverride), 3);
        QCOMPARE(timeline->count(SegmentTimeline::Kind::Segment), 55);

        // The stall is found under the mouse, on top of the segment bars.
        const auto& stall = *std::ranges::find(timeline->items(), SegmentTimeline::Kind::Stall,
                                               &SegmentTimeline::Item::kind);
        timeline->resize(1600, timeline->sizeHint().height());
        const double x = timeline->xAt((stall.start + stall.end) / 2);
        const auto* hit = timeline->itemAt(QPointF(x, 6 + 32 + 13));  // middle of lane 2
        QVERIFY(hit);
        QCOMPARE(hit->kind, SegmentTimeline::Kind::Stall);
        QVERIFY(hit->text.startsWith(QStringLiteral("Stall")));
    }

    void clickingTimelineMovesCursor()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(open(window, chef()));

        SegmentTimeline* timeline = window.timeline();
        TimeCursor* cursor = window.session()->cursor();
        QSignalSpy changed(cursor, &TimeCursor::timeChanged);
        const double target = 20.0;
        const QPoint pos(static_cast<int>(std::lround(timeline->xAt(target))), 20);
        QTest::mouseClick(timeline, Qt::LeftButton, {}, pos);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Timeline);
        const double perPixel = window.session()->endTime() / timeline->width();
        QVERIFY(std::abs(cursor->time() - target) <= perPixel);
    }

    void failedLoadShowsErrors()
    {
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(test::dataPath(QStringLiteral("missing_bundle")));
        QVERIFY(waitFor(opened));
        QVERIFY(!opened.first().at(1).toBool());
        QVERIFY(!window.session()->run());
        QVERIFY(!window.session()->loadErrors().isEmpty());
        QVERIFY(window.summary()->toPlainText().contains(window.session()->loadErrors().first()));
    }

    void secondOpenReplacesFirst()
    {
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(bbb());
        window.openBundle(chef());
        QVERIFY(waitFor(opened));
        QTest::qWait(50);  // a late result for the first bundle would arrive here
        QCOMPARE(opened.size(), 1);
        QCOMPARE(window.session()->run()->info.runId,
                 QStringLiteral("chef_cooking_llm_18Mbps_run_3"));
    }

    void destroyingWindowDuringLoadIsClean()
    {
        for (int i = 0; i < 10; ++i) {
            auto window = std::make_unique<MainWindow>();
            window->openFolder(test::dataPath(QString()));
            window->openBundle(i % 2 ? bbb() : chef());
            if (i % 3 == 0)
                QTest::qWait(i);
            window.reset();  // cancels and waits in shutdown()
        }
    }

    void opensFullBundles()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        MainWindow window;
        QSignalSpy scanned(&window, &MainWindow::folderScanned);
        window.openFolder(test::bundlesPath());
        QVERIFY(waitFor(scanned));
        LibraryModel* model = window.library()->model();
        QCOMPARE(model->rowCount(), 14);
        for (int row = 0; row < model->rowCount(); ++row) {
            const QString directory = model->bundleAt(row)->directory;
            QVERIFY2(open(window, directory), qPrintable(directory));
            const auto& run = window.session()->run();
            QCOMPARE(window.timeline()->count(SegmentTimeline::Kind::Stall),
                     static_cast<int>(run->stalls.size()));
            QCOMPARE(window.timeline()->count(SegmentTimeline::Kind::CutOff),
                     run->playbackCutOff() ? 1 : 0);
            // None of the 14 runs waited with a full buffer (`del` is 0 throughout).
            const auto waits = std::ranges::count_if(run->decisions, [](const Decision& d) {
                return d.bufferFullWait.value_or(0.0) > 0.0;
            });
            QCOMPARE(window.timeline()->count(SegmentTimeline::Kind::BufferFullWait),
                     static_cast<int>(waits));
            // The report is shown for every bundle and agrees with summary.json.
            QVERIFY2(window.report()->report().agreesWithSummary(), qPrintable(directory));
            QVERIFY2(window.report()->verdict().startsWith(QStringLiteral("All 6 numbers")),
                     qPrintable(window.report()->verdict()));
        }
    }

    void reportShowsTheOpenRun()
    {
        MainWindow window;
        QVERIFY(window.report()->verdict().contains(QStringLiteral("No run")));
        QVERIFY(open(window, test::dataPath(QStringLiteral("bbb_36"))));
        const QStringList rows = window.report()->rows();
        QVERIFY(rows.contains(QStringLiteral("What the viewer saw |  |  | ")));
        const auto stalls = std::ranges::find_if(rows, [](const QString& row) {
            return row.startsWith(QStringLiteral("  Stalls |"));
        });
        QVERIFY(stalls != rows.end());
        QVERIFY2(stalls->endsWith(QStringLiteral("| agrees")), qPrintable(*stalls));
        QCOMPARE(window.report()->report().stallCount,
                 static_cast<int>(window.session()->run()->stalls.size()));
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
