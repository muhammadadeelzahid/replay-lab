#include "test_paths.h"

#include "library_view.h"
#include "main_window.h"
#include "segment_timeline.h"
#include "session.h"
#include "summary_panel.h"
#include "time_cursor.h"

#include "abrviz/core/version.h"

#include <QLabel>
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
                              QStringLiteral("&Quit")}));
        QCOMPARE(menus[1]->text(), QStringLiteral("&Playback"));
        QCOMPARE(menuTexts(menus[1]->menu()),
                 (QStringList{QStringLiteral("&Play"), QStringLiteral("&Render Video")}));
        QCOMPARE(menus[2]->menu()->actions().size(), 5);  // one per dock
    }

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
        QVERIFY(model->data(model->index(1, LibraryModel::CutOff), Qt::DisplayRole)
                    .toString()
                    .isEmpty());
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
        }
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
