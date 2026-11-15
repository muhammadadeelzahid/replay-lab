#include "test_paths.h"

#include "format.h"
#include "library_view.h"
#include "log_console.h"
#include "main_window.h"
#include "moment_panel.h"
#include "replay_player.h"
#include "report_view.h"
#include "session.h"
#include "source_list.h"
#include "summary_panel.h"
#include "time_cursor.h"
#include "transport_bar.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/playback/playback_clock.h"

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 60000;

QString chef() { return test::dataPath(QStringLiteral("chef_cooking_18")); }
QString bbb() { return test::dataPath(QStringLiteral("bbb_36")); }

/// A real bundle by folder name; only valid when test::haveFullBundles().
QString realBundle(const char* name)
{
    return QDir(test::bundlesPath()).filePath(QString::fromLatin1(name));
}

/// The smallest real bundle with media (big_buck_bunny at 36 Mbps: 1.8 MB).
QString smallMediaBundle() { return realBundle("big_buck_bunny_llm_36Mbps_run_2"); }

/// True once `spy` has a signal, including one that arrived before the call.
bool waitFor(QSignalSpy& spy, int timeoutMs = kTimeoutMs)
{
    return !spy.isEmpty() || spy.wait(timeoutMs);
}

/// Opens `bundle` and waits for the load and for its log index.
bool openAndIndex(MainWindow& window, const QString& bundle)
{
    QSignalSpy opened(&window, &MainWindow::bundleOpened);
    window.openBundle(bundle);
    if (!waitFor(opened) || !opened.first().at(1).toBool())
        return false;
    // Wait for this run's index. A logIndexReady from an earlier load of the same bundle can
    // arrive while this load is running, so the signal alone does not say the index is there.
    return QTest::qWaitFor([&window] { return window.session()->logIndex() != nullptr; }, kTimeoutMs);
}

bool scan(MainWindow& window, const QString& folder)
{
    QSignalSpy scanned(&window, &MainWindow::folderScanned);
    window.openFolder(folder);
    return waitFor(scanned);
}

/// A window size other than the default that fits the screen (the window keeps itself inside
/// the screen) and is above the window's minimum size (the offscreen screen is small).
QSize smallSize()
{
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    return {available.width() - 40, available.height() * 7 / 8};
}

QAction* findAction(const QWidget& window, const QString& text)
{
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

/// Same rule as SummaryCheck::agrees(): within 1 %, or 0.001 absolute.
bool near(double computed, double exported)
{
    return std::abs(computed - exported) <= std::max(0.001, std::abs(exported) * 0.01);
}

bool copyDir(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
        if (it.fileInfo().isDir()) {
            if (!QDir().mkpath(target))
                return false;
        } else if (!QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// Checks the inspector against the session's clock at the cursor.
void checkMoment(const MainWindow& window, const QString& where)
{
    const Session* session = window.session();
    const auto& run = session->run();
    QVERIFY2(run, qPrintable(where));
    const double time = session->cursor()->time();
    QVERIFY2(time >= 0.0 && time <= session->endTime(), qPrintable(where));
    const auto position = session->clock().positionAtSim(time);
    const QString segment = window.moment()->value(QStringLiteral("Segment"));
    if (position.beforeStart) {
        QVERIFY2(segment.startsWith(QStringLiteral("startup")), qPrintable(where + segment));
    } else {
        QCOMPARE(segment, QStringLiteral("%1 of %2").arg(position.segment).arg(run->video.segmentCount));
    }
    const QString player = window.moment()->value(QStringLiteral("Player"));
    QCOMPARE(player.contains(QStringLiteral("stalled")), position.stalled);
    QCOMPARE(window.moment()->value(QStringLiteral("Simulation")), format::seconds(time));
}

/// Compares the report of the open run with the bundle's own summary.json.
void checkReportAgainstSummaryJson(const MainWindow& window)
{
    const auto& run = window.session()->run();
    QVERIFY(run);
    const QJsonDocument doc = QJsonDocument::fromJson(
        readFile(QDir(run->info.bundleDirectory).filePath(QStringLiteral("summary.json"))));
    const QJsonObject ue = doc.object().value(QStringLiteral("ues")).toArray().first().toObject();
    QVERIFY(!ue.isEmpty());
    const QoeReport& report = window.report()->report();
    const QString id = run->info.runId;
    QCOMPARE(report.stallCount, ue.value(QStringLiteral("stall_count")).toInt());
    QCOMPARE(report.chosenSwitches, ue.value(QStringLiteral("switch_count")).toInt());
    QVERIFY2(near(report.rebufferTime, ue.value(QStringLiteral("rebuffer_time_s")).toDouble()), qPrintable(id));
    QVERIFY2(near(report.startupDelay, ue.value(QStringLiteral("startup_delay_s")).toDouble()), qPrintable(id));
    QVERIFY2(near(report.averageChosenBitrate, ue.value(QStringLiteral("avg_bitrate_bps")).toDouble()),
             qPrintable(id));
    QVERIFY2(near(report.averageThroughput, ue.value(QStringLiteral("avg_throughput_bps")).toDouble()),
             qPrintable(id));
    // The report shown is the session's.
    QCOMPARE(report.stallCount, window.session()->report().stallCount);
    QCOMPARE(report.stallCount, static_cast<int>(run->stalls.size()));
    // Every row compared with summary.json says it agrees.
    int compared = 0;
    for (const QString& row : window.report()->rows()) {
        const QStringList cells = row.split(QStringLiteral(" | "));
        if (cells.size() == 4 && !cells[2].isEmpty()) {
            ++compared;
            QVERIFY2(cells[3] == QStringLiteral("agrees"), qPrintable(id + QStringLiteral(": ") + row));
        }
    }
    QCOMPARE(compared, 6);
}

/// Filters the logs and checks every row shown matches the filter.
void checkLogFilter(MainWindow& window, const QString& text, const QString& mustContain,
                    const QString& mustNotContain)
{
    LogConsole* logs = window.logs();
    QSignalSpy applied(logs, &LogConsole::filterApplied);
    logs->filterEdit()->setText(text);
    QVERIFY(waitFor(applied));
    QTRY_VERIFY_WITH_TIMEOUT(!logs->isFiltering(), kTimeoutMs);
    LogModel* model = logs->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QString line = model->data(model->index(row, LogModel::Text), Qt::DisplayRole).toString();
        QVERIFY2(line.contains(mustContain, Qt::CaseInsensitive), qPrintable(line));
        if (!mustNotContain.isEmpty())
            QVERIFY2(!line.contains(mustNotContain, Qt::CaseInsensitive), qPrintable(line));
    }
}

/// The times the sweep visits: even steps across the run, every segment start and every stall
/// boundary (and a hair either side of each).
std::vector<double> sweepTimes(const Session& session, int steps)
{
    std::vector<double> times;
    const double end = session.endTime();
    for (int i = 0; i <= steps; ++i)
        times.push_back(end * i / steps);
    for (const auto& piece : session.clock().pieces()) {
        times.push_back(piece.simStart);
        if (piece.kind == PlaybackClock::Piece::Kind::Stall) {
            for (double t : {piece.simStart - 1e-3, piece.simStart + 1e-3, piece.simEnd - 1e-3,
                             piece.simEnd, piece.simEnd + 1e-3})
                times.push_back(std::clamp(t, 0.0, end));
        }
    }
    return times;
}

#ifndef ABRVIZ_APP_PATH
#define ABRVIZ_APP_PATH ""
#endif

struct AppRun {
    bool finished = false;
    QProcess::ExitStatus status = QProcess::CrashExit;
    int exitCode = -1;
    QString out;
    QString err;
};

/// Runs the real application, always offscreen and with its settings in `settingsDir`.
AppRun runApp(const QStringList& args, const QString& settingsDir, int timeoutMs = 120000)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("ABRVIZ_SETTINGS_DIR"), settingsDir);
    if (!env.contains(QStringLiteral("ABRVIZ_RENDER_CACHE")))
        env.insert(QStringLiteral("ABRVIZ_RENDER_CACHE"), QDir(settingsDir).filePath(QStringLiteral("render-cache")));
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(ABRVIZ_APP_PATH), args);
    AppRun result;
    result.finished = process.waitForStarted(30000) && process.waitForFinished(timeoutMs);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(5000);
    }
    result.status = process.exitStatus();
    result.exitCode = process.exitCode();
    result.out = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.err = QString::fromLocal8Bit(process.readAllStandardError());
    return result;
}

QString describe(const AppRun& run)
{
    return QStringLiteral("finished %1, status %2, exit %3\nstdout: %4\nstderr: %5")
        .arg(run.finished)
        .arg(static_cast<int>(run.status))
        .arg(run.exitCode)
        .arg(run.out.left(2000), run.err.left(2000));
}

#define SKIP_UNLESS_CLI()                                                                       \
    do {                                                                                        \
        if (qEnvironmentVariableIsSet("ABRVIZ_INTEGRATION_SKIP_CLI"))                          \
            QSKIP("command-line tests run in the cli_app CTest entry");                        \
        if (!QFileInfo::exists(QStringLiteral(ABRVIZ_APP_PATH)))                                \
            QSKIP("application binary not built");                                              \
    } while (false)

} // namespace

class TestIntegration : public QObject {
    Q_OBJECT

    QTemporaryDir settingsDir_;

private slots:
    void initTestCase()
    {
        // Never touch the user's settings: an INI file in a temporary folder, under a name of
        // our own.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("abrviz-integration-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_integration"));
        QVERIFY(settingsDir_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QSettings().clear();
    }

    void init()
    {
        QSettings().clear();
        // Sweeps must not decode video: the pre-rendered engine does nothing until prepared.
        QSettings().setValue(QStringLiteral("replay/engine"), QStringLiteral("prerendered"));
    }

    // ---- Every real bundle, end to end ------------------------------------------------------

    void sweepsEveryRealBundle()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QVERIFY(window.setEngine(Engine::Prerendered));
        QVERIFY(scan(window, test::bundlesPath()));
        LibraryModel* model = window.library()->model();
        QCOMPARE(model->rowCount(), 14);
        for (int row = 0; row < model->rowCount(); ++row) {
            const BundleSummaryPtr bundle = model->bundleAt(row);
            QVERIFY(bundle->errors.isEmpty());
            const MainWindow::Page pageBefore = window.currentPage();
            QVERIFY2(openAndIndex(window, bundle->directory), qPrintable(bundle->directory));
            const auto& run = window.session()->run();
            QCOMPARE(run->info.bundleDirectory, bundle->directory);
            QCOMPARE(run->info.runId, bundle->runId);
            QCOMPARE(static_cast<int>(run->stalls.size()), bundle->stalls);
            // Opening from the Library shows the replay; any other page stays.
            QCOMPARE(window.currentPage(), pageBefore == MainWindow::Page::Library ? MainWindow::Page::Replay
                                                                                 : pageBefore);
            QVERIFY(window.windowTitle().startsWith(run->video.title));
            QVERIFY(window.summary()->toPlainText().contains(run->info.runId));
            QCOMPARE(window.sidebar()->runCount(), 14);

            // Sweep the cursor, changing page as it goes.
            const auto times = sweepTimes(*window.session(), 150);
            for (std::size_t i = 0; i < times.size(); ++i) {
                if (i % 25 == 0)
                    window.showPage(static_cast<MainWindow::Page>(1 + (i / 25) % 4));
                window.session()->cursor()->setTime(times[i]);
                checkMoment(window, QStringLiteral("%1 at %2: ").arg(run->info.runId).arg(times[i]));
                if (QTest::currentTestFailed())
                    return;
            }
            // Segment steps land on segment starts and agree with the inspector.
            window.session()->cursor()->setTime(0.0);
            for (int step = 0; step < run->video.segmentCount + 2; ++step) {
                window.stepSegment(1);
                checkMoment(window, run->info.runId + QStringLiteral(" stepping: "));
                if (QTest::currentTestFailed())
                    return;
            }

            // Every page.
            for (int p = 0; p < MainWindow::kPageCount; ++p) {
                window.showPage(static_cast<MainWindow::Page>(p));
                QCOMPARE(window.currentPage(), static_cast<MainWindow::Page>(p));
                QCoreApplication::processEvents();
            }

            // Logs: words, an exclusion, and back to everything.
            window.showPage(MainWindow::Page::Logs);
            checkLogFilter(window, QStringLiteral("segment"), QStringLiteral("segment"), {});
            checkLogFilter(window, QStringLiteral("segment -rung"), QStringLiteral("segment"),
                           QStringLiteral("rung"));
            checkLogFilter(window, QString(), QString(), {});
            QVERIFY(window.logs()->model()->rowCount() > 0);

            // Report against summary.json.
            window.showPage(MainWindow::Page::Report);
            checkReportAgainstSummaryJson(window);
            if (QTest::currentTestFailed())
                return;
        }
    }

    // ---- Rapid sequences ---------------------------------------------------------------------

    void openingBBeforeAFinishesShowsB()
    {
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        QSignalSpy indexed(&window, &MainWindow::logIndexReady);
        const QString a = test::haveFullBundles() ? realBundle("big_buck_bunny_llm_18Mbps_run_3") : bbb();
        window.openBundle(a);
        window.openBundle(chef());
        QVERIFY(waitFor(opened));
        QVERIFY(waitFor(indexed));
        QTest::qWait(100);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(QDir(opened.first().at(0).toString()), QDir(chef()));
        QCOMPARE(window.session()->run()->info.runId, QStringLiteral("chef_cooking_llm_18Mbps_run_3"));
        QCOMPARE(window.report()->report().stallCount, 0);
        checkMoment(window, QStringLiteral("after A then B: "));
        QVERIFY(window.logs()->model()->rowCount() > 0 || window.logs()->isFiltering());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
    }

    void openingTheSameBundleTwice()
    {
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(bbb());
        window.openBundle(bbb());
        QVERIFY(waitFor(opened));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
        QCOMPARE(opened.size(), 1);
        const auto first = window.session()->run();
        QVERIFY(first);
        // Once more after it finished: a fresh load of the same run.
        window.session()->cursor()->setTime(30.0);
        QVERIFY(openAndIndex(window, bbb()));
        QVERIFY(window.session()->run() != first);
        QCOMPARE(window.session()->run()->info.runId, first->info.runId);
        QCOMPARE(window.session()->cursor()->time(), window.session()->run()->playbackStart);
        QCOMPARE(window.report()->report().stallCount, 1);
        checkMoment(window, QStringLiteral("reopened: "));
    }

    void folderThenBundleFromAnotherFolder()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        MainWindow window;
        QVERIFY(scan(window, test::dataPath(QString())));
        QCOMPARE(window.library()->model()->rowCount(), 2);
        const QString other = smallMediaBundle();
        QVERIFY(openAndIndex(window, other));
        QCOMPARE(window.session()->run()->info.bundleDirectory, QDir(other).absolutePath());
        // The library still lists the folder that was opened.
        QCOMPARE(window.library()->model()->rowCount(), 2);
        QCOMPARE(window.sidebar()->runCount(), 2);
        QCOMPARE(window.library()->model()->rowOf(other), -1);
        QVERIFY(window.windowTitle().startsWith(window.session()->run()->video.title));
        checkReportAgainstSummaryJson(window);
        // Now the run's own folder: it is listed, and the open run stays open.
        QVERIFY(scan(window, test::bundlesPath()));
        QCOMPARE(window.library()->model()->rowCount(), 14);
        QCOMPARE(window.sidebar()->runCount(), 14);
        QVERIFY(window.library()->model()->rowOf(QDir(other).absolutePath()) >= 0);
        QCOMPARE(window.session()->run()->info.bundleDirectory, QDir(other).absolutePath());
        // Folder and bundle started together, from different folders.
        QSignalSpy scanned(&window, &MainWindow::folderScanned);
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openFolder(test::dataPath(QString()));
        window.openBundle(realBundle("chef_cooking_llm_9Mbps_run_4"));
        QVERIFY(waitFor(scanned));
        QVERIFY(waitFor(opened));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
        QCOMPARE(window.library()->model()->rowCount(), 2);
        QCOMPARE(window.session()->run()->info.runId, QStringLiteral("chef_cooking_llm_9Mbps_run_4"));
    }

    void closeAndReopen()
    {
        QSize size;
        {
            MainWindow window;
            window.resize(smallSize());
            size = window.size();  // as the window allows it
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(scan(window, test::dataPath(QString())));
            QVERIFY(openAndIndex(window, chef()));
            QVERIFY(window.close());
            QVERIFY(!window.isVisible());
            // Shown again, the window still works.
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(openAndIndex(window, bbb()));
            window.showPage(MainWindow::Page::Analysis);
            window.stepReplay(5.0);
            checkMoment(window, QStringLiteral("shown again: "));
            QVERIFY(window.close());
        }
        // A new window: no run open, the folder is in the recent list, and opening works.
        MainWindow window;
        QCOMPARE(window.currentPage(), MainWindow::Page::Library);
        QVERIFY(!window.session()->run());
        // The height is restored (the width is held up by the sidebar's minimum on the small
        // offscreen screen).
        QCOMPARE(window.height(), size.height());
        QVERIFY(openAndIndex(window, chef()));
        checkMoment(window, QStringLiteral("new window: "));
    }

    void switchingEnginesMidPlayback()
    {
#if ABRVIZ_WITH_GSTREAMER
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.setEngine(Engine::GStreamer));
        QVERIFY(openAndIndex(window, smallMediaBundle()));
        ReplayPlayer* player = window.player();
        QTRY_VERIFY_WITH_TIMEOUT(player->engine()->isReady(), kTimeoutMs);
        for (int round = 0; round < 3; ++round) {
            QVERIFY(window.setEngine(Engine::GStreamer));
            QTRY_VERIFY_WITH_TIMEOUT(player->engine()->isReady(), kTimeoutMs);
            QTRY_VERIFY_WITH_TIMEOUT(player->engine()->state() != ReplaySource::State::Preparing, kTimeoutMs);
            const double before = window.session()->cursor()->time();
            player->play();
            QTRY_VERIFY_WITH_TIMEOUT(window.session()->cursor()->time() > before + 0.2, kTimeoutMs);
            QVERIFY(player->isRunning());
            // Mid-playback: the other engine, then a page change and a seek.
            QVERIFY(window.setEngine(Engine::Prerendered));
            QCOMPARE(player->engineKind(), std::optional<Engine>(Engine::Prerendered));
            QVERIFY(!player->isRunning());
            window.showPage(static_cast<MainWindow::Page>(1 + round));
            window.stepSegment(1);
            checkMoment(window, QStringLiteral("after the switch: "));
            QCOMPARE(window.moment()->value(QStringLiteral("Engine")), player->engine()->name());
        }
        // Destroyed while playing.
        QVERIFY(window.setEngine(Engine::GStreamer));
        QTRY_VERIFY_WITH_TIMEOUT(player->engine()->isReady()
                                     && player->engine()->state() != ReplaySource::State::Preparing,
                                 kTimeoutMs);
        player->play();
        QTest::qWait(300);
#else
        // One engine only: asking for GStreamer mid-load keeps the pre-rendered one.
        MainWindow window;
        window.openBundle(bbb());
        QVERIFY(!window.setEngine(Engine::GStreamer));
        QVERIFY(openAndIndex(window, chef()));
        QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::Prerendered));
        QSKIP("GStreamer is not built; only the pre-rendered engine exists");
#endif
    }

    void changingPagesDuringLoading()
    {
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(bbb());
        for (int i = 0; i < 3 * MainWindow::kPageCount; ++i)
            window.showPage(static_cast<MainWindow::Page>(i % MainWindow::kPageCount));
        window.showPage(MainWindow::Page::Logs);
        QVERIFY(waitFor(opened));
        // A page other than the Library stays chosen; the Library would give way to the replay.
        QCOMPARE(window.currentPage(), MainWindow::Page::Logs);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);

        // Report shown during the load of another run: it follows the new run.
        window.showPage(MainWindow::Page::Report);
        QCOMPARE(window.report()->report().stallCount, 1);
        opened.clear();
        window.openBundle(chef());
        window.showPage(MainWindow::Page::Library);
        QVERIFY(waitFor(opened));
        QCOMPARE(window.currentPage(), MainWindow::Page::Replay);
        QCOMPARE(window.report()->report().stallCount, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
        checkMoment(window, QStringLiteral("after the page changes: "));
    }

    void destroyingTheWindowAtRandomPoints()
    {
        QRandomGenerator rng(20261114);
        QStringList bundles{bbb(), chef(), test::dataPath(QStringLiteral("missing_bundle"))};
        QStringList folders{test::dataPath(QString())};
        if (test::haveFullBundles()) {
            bundles << realBundle("chef_cooking_llm_9Mbps_run_4") << smallMediaBundle();
            folders << test::bundlesPath();
        }
        const int iterations = 30;
        for (int i = 0; i < iterations; ++i) {
            auto window = std::make_unique<MainWindow>();
            if (rng.bounded(2) != 0)
                window->show();
            const int ops = static_cast<int>(rng.bounded(1, 12));
            for (int op = 0; op < ops; ++op) {
                switch (rng.bounded(8)) {
                case 0:
                    window->openFolder(folders[static_cast<int>(rng.bounded(static_cast<int>(folders.size())))]);
                    break;
                case 1:
                case 2:
                    window->openBundle(bundles[static_cast<int>(rng.bounded(static_cast<int>(bundles.size())))]);
                    break;
                case 3:
                    window->showPage(static_cast<MainWindow::Page>(rng.bounded(MainWindow::kPageCount)));
                    break;
                case 4:
                    window->session()->cursor()->setTime(rng.bounded(200.0));
                    window->stepSegment(rng.bounded(2) != 0 ? 1 : -1);
                    break;
                case 5:
                    window->logs()->filterEdit()->setText(rng.bounded(2) != 0 ? QStringLiteral("segment")
                                                                              : QString());
                    break;
                case 6:
                    window->setEngine(rng.bounded(2) != 0 ? Engine::GStreamer : Engine::Prerendered);
                    break;
                default:
                    window->stepReplay(rng.bounded(2) != 0 ? 5.0 : -5.0);
                    break;
                }
                QTest::qWait(static_cast<int>(rng.bounded(25)));
            }
            if (window->session()->run())
                checkMoment(*window, QStringLiteral("iteration %1: ").arg(i));
            window.reset();
        }
    }

    // ---- Broken inputs -----------------------------------------------------------------------

    void folderWithGoodAndCorruptBundles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir root(dir.path());
        const auto copy = [&](const QString& from, const QString& name) {
            return copyDir(from, root.filePath(name)) ? root.filePath(name) : QString();
        };
        QVERIFY(!copy(bbb(), QStringLiteral("good_bbb")).isEmpty());
        QVERIFY(!copy(chef(), QStringLiteral("good_chef")).isEmpty());

        // Each corrupt bundle: a copy of chef with one file damaged or removed.
        struct Damage {
            const char* name;
            const char* file;
            QByteArray contents;  // null: remove the file or folder
        };
        const QByteArray runJson = readFile(QDir(chef()).filePath(QStringLiteral("run.json")));
        const std::vector<Damage> damages{
            {"bad_run_garbage", "run.json", QByteArrayLiteral("{not json")},
            {"bad_run_empty", "run.json", QByteArrayLiteral("")},
            {"bad_run_array", "run.json", QByteArrayLiteral("[1, 2, 3]")},
            {"bad_run_truncated", "run.json", runJson.left(runJson.size() / 2)},
            {"bad_summary", "summary.json", QByteArrayLiteral("\x00\x01garbage")},
            {"bad_segments", "segments.jsonl", QByteArrayLiteral("{\"segment\": \"x\"}\n\xff\xfe\n")},
            {"bad_events", "player_events.jsonl", QByteArrayLiteral("{\"event\": 12}\n{")},
            {"bad_samples", "samples.jsonl", QByteArrayLiteral("[]\n\"text\"\n")},
            {"bad_no_videos", "videos", QByteArray()},
            {"bad_no_segments", "segments.jsonl", QByteArray()},
        };
        QStringList bad;
        for (const Damage& damage : damages) {
            const QString bundle = copy(chef(), QString::fromLatin1(damage.name));
            QVERIFY(!bundle.isEmpty());
            const QString target = QDir(bundle).filePath(QString::fromLatin1(damage.file));
            if (damage.contents.isNull()) {
                QVERIFY(QFileInfo(target).isDir() ? QDir(target).removeRecursively() : QFile::remove(target));
            } else {
                QVERIFY(writeFile(target, damage.contents));
            }
            bad << bundle;
        }
        // Seeded random byte damage in random files.
        QRandomGenerator rng(7);
        const QStringList files{QStringLiteral("run.json"), QStringLiteral("segments.jsonl"),
                                QStringLiteral("player_events.jsonl"), QStringLiteral("samples.jsonl"),
                                QStringLiteral("summary.json"),
                                QStringLiteral("videos/chef_cooking/video.json")};
        QStringList fuzzed;
        for (int i = 0; i < 8; ++i) {
            const QString bundle = copy(chef(), QStringLiteral("fuzz_%1").arg(i));
            QVERIFY(!bundle.isEmpty());
            const QString file = QDir(bundle).filePath(files[static_cast<int>(rng.bounded(static_cast<int>(files.size())))]);
            QByteArray bytes = readFile(file);
            QVERIFY(!bytes.isEmpty());
            for (int k = 0; k < 6; ++k)
                bytes[static_cast<int>(rng.bounded(static_cast<int>(bytes.size())))] = static_cast<char>(rng.bounded(256));
            QVERIFY(writeFile(file, bytes));
            fuzzed << bundle;
        }
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("not_a_bundle/logs"))));

        MainWindow window;
        window.show();
        QVERIFY(scan(window, dir.path()));
        LibraryModel* model = window.library()->model();
        // Every folder with a run.json is listed; the folder without one is not.
        QCOMPARE(model->rowCount(), 2 + static_cast<int>(bad.size() + fuzzed.size()));
        QCOMPARE(window.sidebar()->runCount(), model->rowCount());
        QCOMPARE(model->rowOf(root.filePath(QStringLiteral("not_a_bundle"))), -1);

        for (const QString& good : {root.filePath(QStringLiteral("good_bbb")), root.filePath(QStringLiteral("good_chef"))}) {
            const int row = model->rowOf(good);
            QVERIFY(row >= 0);
            QVERIFY2(model->bundleAt(row)->errors.isEmpty(), qPrintable(model->bundleAt(row)->errors.join(u'\n')));
            QVERIFY(openAndIndex(window, good));
            QVERIFY(window.session()->loadErrors().isEmpty());
        }

        // A corrupt bundle shows its problem: in the library, or when it is opened.
        for (const QString& bundle : bad) {
            const int row = model->rowOf(bundle);
            QVERIFY2(row >= 0, qPrintable(bundle));
            const BundleSummaryPtr summary = model->bundleAt(row);
            if (!summary->errors.isEmpty()) {
                window.showPage(MainWindow::Page::Library);
                window.library()->selectBundle(bundle);
                QVERIFY2(window.library()->detailsText().contains(summary->errors.first().toHtmlEscaped())
                             || window.library()->detailsText().contains(summary->errors.first()),
                         qPrintable(window.library()->detailsText()));
            }
            QSignalSpy opened(&window, &MainWindow::bundleOpened);
            window.openBundle(bundle);
            QVERIFY(waitFor(opened));
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
            const bool ok = opened.first().at(1).toBool();
            const QStringList& errors = window.session()->loadErrors();
            const bool warned = ok && !window.session()->run()->warnings.empty();
            QVERIFY2(!summary->errors.isEmpty() || !ok || !errors.isEmpty() || warned,
                     qPrintable(QStringLiteral("no problem reported for %1").arg(QDir(bundle).dirName())));
            if (!ok) {
                QVERIFY(!window.session()->run());
                QVERIFY(!errors.isEmpty());
                QVERIFY(window.summary()->toPlainText().contains(errors.first()));
                QVERIFY(window.noticeText().contains(QStringLiteral("Could not open")));
            } else {
                checkMoment(window, QDir(bundle).dirName() + QStringLiteral(": "));
                for (int p = 0; p < MainWindow::kPageCount; ++p)
                    window.showPage(static_cast<MainWindow::Page>(p));
            }
        }
        // Randomly damaged ones: whatever they hold, nothing crashes.
        for (const QString& bundle : fuzzed) {
            QSignalSpy opened(&window, &MainWindow::bundleOpened);
            window.openBundle(bundle);
            QVERIFY(waitFor(opened));
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), kTimeoutMs);
            if (window.session()->run()) {
                window.session()->cursor()->setTime(window.session()->endTime() / 2);
                checkMoment(window, QDir(bundle).dirName() + QStringLiteral(": "));
                for (int p = 0; p < MainWindow::kPageCount; ++p)
                    window.showPage(static_cast<MainWindow::Page>(p));
            }
        }
        // The good ones still open after all that.
        QVERIFY(openAndIndex(window, root.filePath(QStringLiteral("good_bbb"))));
        QCOMPARE(window.report()->report().stallCount, 1);
    }

    // ---- Settings ----------------------------------------------------------------------------

    void settingsPersistAcrossWindows()
    {
        const QString folder = QDir(test::dataPath(QString())).absolutePath();
        QSize size;
        {
            MainWindow window;
            QVERIFY(window.setEngine(Engine::Prerendered, true));
            QAction* overlay = findAction(window, QStringLiteral("Show Video Overlay"));
            QAction* sidebar = findAction(window, QStringLiteral("Show Sidebar"));
            QAction* inspector = findAction(window, QStringLiteral("Show Inspector"));
            QVERIFY(overlay && sidebar && inspector);
            overlay->setChecked(true);
            sidebar->setChecked(false);
            inspector->setChecked(false);
            QVERIFY(scan(window, folder));
            QVERIFY(scan(window, QDir(bbb()).absolutePath()));
            window.resize(smallSize() - QSize(0, 20));
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            size = window.size();
            window.close();
        }
        // Written where we said, not in the user's settings.
        QVERIFY(QFileInfo(QSettings().fileName()).absoluteFilePath().startsWith(settingsDir_.path()));
        {
            MainWindow window;
            QCOMPARE(MainWindow::savedEngine(), Engine::Prerendered);
            QCOMPARE(window.player()->engineKind(), std::optional<Engine>(Engine::Prerendered));
            QVERIFY(findAction(window, QStringLiteral("Show Video Overlay"))->isChecked());
            QVERIFY(window.player()->isOverlayVisible());
            QVERIFY(!findAction(window, QStringLiteral("Show Sidebar"))->isChecked());
            QVERIFY(!findAction(window, QStringLiteral("Show Inspector"))->isChecked());
            QCOMPARE(window.size(), size);
            // The recent folders, newest first, in Open Recent.
            QAction* recent = findAction(window, QStringLiteral("Open Recent"));
            QVERIFY(recent && recent->menu() && recent->isEnabled());
            QStringList names;
            for (QAction* action : recent->menu()->actions()) {
                if (!action->isSeparator())
                    names << action->text();
            }
            QCOMPARE(names, (QStringList{QStringLiteral("bbb_36"), QDir(folder).dirName(),
                                         QStringLiteral("Clear Menu")}));
            // Opening a recent folder from the menu lists it.
            QSignalSpy scanned(&window, &MainWindow::folderScanned);
            recent->menu()->actions().at(1)->trigger();
            QVERIFY(waitFor(scanned));
            QCOMPARE(window.library()->model()->rowCount(), 2);
            // Clear Menu empties it, for this window and the next.
            QAction* clear = nullptr;
            for (QAction* action : recent->menu()->actions()) {
                if (action->text() == QStringLiteral("Clear Menu"))
                    clear = action;
            }
            QVERIFY(clear);
            clear->trigger();
            QVERIFY(!recent->isEnabled());
            // The inspector stays hidden with a run open.
            QVERIFY(openAndIndex(window, chef()));
            QVERIFY(!window.moment()->isVisibleTo(&window));
        }
        MainWindow window;
        QVERIFY(!findAction(window, QStringLiteral("Open Recent"))->isEnabled());
    }

    // ---- The application binary ---------------------------------------------------------------

    void cliHelpAndVersion()
    {
        SKIP_UNLESS_CLI();
#ifdef Q_OS_WIN
        QSKIP("a Windows GUI program shows --help and --version in a message box");
#endif
        QTemporaryDir settings;
        const QString shot = QDir(settings.path()).filePath(QStringLiteral("unused.png"));
        AppRun help = runApp({QStringLiteral("--help"), QStringLiteral("--screenshot"), shot}, settings.path());
        QVERIFY2(help.finished && help.status == QProcess::NormalExit && help.exitCode == 0, qPrintable(describe(help)));
        for (const char* option : {"--screenshot", "--engine", "--page", "--profile", "--record", "--duration"})
            QVERIFY2(help.out.contains(QLatin1String(option)), option);
        AppRun version = runApp({QStringLiteral("--version"), QStringLiteral("--screenshot"), shot}, settings.path());
        QVERIFY2(version.finished && version.status == QProcess::NormalExit && version.exitCode == 0,
                 qPrintable(describe(version)));
        QVERIFY(version.out.contains(QStringLiteral("Stream Inspector")));
        QVERIFY(!QFileInfo::exists(shot));
    }

    void cliRejectsUnknownPageAndEngine()
    {
        SKIP_UNLESS_CLI();
        QTemporaryDir settings;
        const QString shot = QDir(settings.path()).filePath(QStringLiteral("unused.png"));
        AppRun page = runApp({QStringLiteral("--screenshot"), shot, QStringLiteral("--page"), QStringLiteral("bogus"), bbb()},
                             settings.path());
        QVERIFY2(page.finished && page.status == QProcess::NormalExit && page.exitCode == 2, qPrintable(describe(page)));
        QVERIFY(page.err.contains(QStringLiteral("unknown page 'bogus'")));
        AppRun engine = runApp({QStringLiteral("--screenshot"), shot, QStringLiteral("--engine"), QStringLiteral("bogus"), bbb()},
                               settings.path());
        QVERIFY2(engine.finished && engine.status == QProcess::NormalExit && engine.exitCode == 2,
                 qPrintable(describe(engine)));
        QVERIFY(engine.err.contains(QStringLiteral("engine 'bogus' is not available")));
#if !ABRVIZ_WITH_GSTREAMER
        AppRun missing = runApp({QStringLiteral("--screenshot"), shot, QStringLiteral("--engine"), QStringLiteral("gstreamer")},
                                settings.path());
        QVERIFY2(missing.finished && missing.exitCode == 2, qPrintable(describe(missing)));
#endif
        QVERIFY(!QFileInfo::exists(shot));
    }

    void cliProfileAndRecordNeedABundle()
    {
        SKIP_UNLESS_CLI();
#ifdef Q_OS_WIN
        QSKIP("a Windows GUI program shows the usage in a message box");
#endif
        QTemporaryDir settings;
        const QString frames = QDir(settings.path()).filePath(QStringLiteral("frames"));
        for (const QStringList& args : {QStringList{QStringLiteral("--profile"), QStringLiteral("1")},
                                        QStringList{QStringLiteral("--record"), frames},
                                        QStringList{QStringLiteral("--profile"), QStringLiteral("1"), test::dataPath(QString())},
                                        QStringList{QStringLiteral("--record"), frames, test::dataPath(QString())}}) {
            AppRun run = runApp(args, settings.path());
            QVERIFY2(run.finished && run.status == QProcess::NormalExit && run.exitCode == 1,
                     qPrintable(args.join(u' ') + u'\n' + describe(run)));
            QVERIFY(run.out.contains(QStringLiteral("Usage:")) || run.err.contains(QStringLiteral("Usage:")));
        }
        QVERIFY(!QFileInfo::exists(frames));
    }

    void cliScreenshotWritesAPng()
    {
        SKIP_UNLESS_CLI();
        QTemporaryDir settings;
        struct Case {
            QStringList args;
            QSize size;
        };
        const QString folder = test::dataPath(QString());
        const std::vector<Case> cases{
            {{bbb(), QStringLiteral("--engine"), QStringLiteral("prerendered"), QStringLiteral("--size"),
              QStringLiteral("1200x760"), QStringLiteral("--time"), QStringLiteral("40")},
             QSize(1200, 760)},
            {{folder, QStringLiteral("--bundle"), QStringLiteral("chef_cooking_18"), QStringLiteral("--page"),
              QStringLiteral("report"), QStringLiteral("--size"), QStringLiteral("1000x700")},
             QSize(1000, 700)},
            {{folder, QStringLiteral("--page"), QStringLiteral("logs")}, QSize()},
            {{}, QSize()},  // the welcome screen
        };
        int index = 0;
        for (const Case& c : cases) {
            const QString shot = QDir(settings.path()).filePath(QStringLiteral("shot-%1.png").arg(index++));
            AppRun run = runApp(QStringList{QStringLiteral("--screenshot"), shot} + c.args, settings.path());
            QVERIFY2(run.finished && run.status == QProcess::NormalExit && run.exitCode == 0,
                     qPrintable(c.args.join(u' ') + u'\n' + describe(run)));
            QVERIFY(run.out.contains(QStringLiteral("saved ")));
            QVERIFY(QFileInfo(shot).size() > 0);
            const QImage image(shot);
            QVERIFY(!image.isNull());
            if (c.size.isValid()) {
                // The picture is at least the window's size in points (larger on a hi-dpi screen).
                QVERIFY2(image.width() >= c.size.width() && image.height() >= c.size.height(),
                         qPrintable(QStringLiteral("%1x%2").arg(image.width()).arg(image.height())));
            }
        }
        // The app kept its settings in the folder it was given.
        QSettings saved(QDir(settings.path()).filePath(QStringLiteral("muhammadadeelzahid.github.io/Stream Inspector.ini")),
                        QSettings::IniFormat);
        QVERIFY2(saved.value(QStringLiteral("library/recentFolders")).toStringList().contains(folder)
                     || saved.value(QStringLiteral("library/recentFolders")).toStringList().contains(QDir(folder).absolutePath()),
                 qPrintable(saved.fileName()));
    }

    void cliRecordWritesNumberedFrames()
    {
        SKIP_UNLESS_CLI();
        QTemporaryDir settings;
        // A bundle without media cannot be played: the engine fails and the app says so.
        {
            const QString frames = QDir(settings.path()).filePath(QStringLiteral("no-media"));
            AppRun run = runApp({QStringLiteral("--record"), frames, QStringLiteral("--duration"), QStringLiteral("1"),
                                 QStringLiteral("--engine"), QStringLiteral("prerendered"), bbb()},
                                settings.path());
            QVERIFY2(run.finished && run.status == QProcess::NormalExit && run.exitCode == 1, qPrintable(describe(run)));
            QVERIFY(run.err.contains(QStringLiteral("engine error")));
        }
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present (recording needs a bundle with media)");
#if ABRVIZ_WITH_GSTREAMER
        const QString engine = QStringLiteral("gstreamer");
#else
#ifdef Q_OS_MACOS
        // Qt's AVFoundation backend, which the pre-rendered engine plays through, needs the Cocoa
        // run loop; offscreen it never gets ready.
        QSKIP("recording on macOS without GStreamer needs the Cocoa platform");
#endif
        const QString engine = QStringLiteral("prerendered");  // renders once into the build folder
#endif
        const QString frames = QDir(settings.path()).filePath(QStringLiteral("frames"));
        AppRun run = runApp({QStringLiteral("--record"), frames, QStringLiteral("--duration"), QStringLiteral("1"),
                             QStringLiteral("--engine"), engine, QStringLiteral("--size"), QStringLiteral("800x500"),
                             smallMediaBundle()},
                            settings.path(), 280000);
        QVERIFY2(run.finished && run.status == QProcess::NormalExit && run.exitCode == 0, qPrintable(describe(run)));
        const QStringList saved = QDir(frames).entryList({QStringLiteral("frame-*.png")}, QDir::Files, QDir::Name);
        QVERIFY2(saved.size() >= 3, qPrintable(saved.join(u' ')));
        QVERIFY(run.out.contains(QStringLiteral("saved %1 pictures").arg(saved.size())));
        // Every picture is at the window's size: the size asked for, or more where the window's
        // minimum size is larger.
        const QSize first = QImage(QDir(frames).filePath(saved.first())).size();
        QVERIFY2(first.width() >= 800 && first.height() >= 500, qPrintable(QStringLiteral("%1x%2").arg(first.width()).arg(first.height())));
        for (int i = 0; i < saved.size(); ++i) {
            QCOMPARE(saved[i], QStringLiteral("frame-%1.png").arg(i, 4, 10, QLatin1Char('0')));
            const QImage image(QDir(frames).filePath(saved[i]));
            QCOMPARE(image.size(), first);
        }
    }

    void cliNonexistentPathDoesNotCrash()
    {
        SKIP_UNLESS_CLI();
        QTemporaryDir settings;
        const QString nowhere = QDir(settings.path()).filePath(QStringLiteral("does/not/exist"));
        const QString shot = QDir(settings.path()).filePath(QStringLiteral("nowhere.png"));
        // A folder that does not exist: an empty library is grabbed.
        AppRun folder = runApp({QStringLiteral("--screenshot"), shot, nowhere}, settings.path());
        QVERIFY2(folder.finished && folder.status == QProcess::NormalExit && folder.exitCode == 0,
                 qPrintable(describe(folder)));
        QVERIFY(QFileInfo(shot).size() > 0);
        // A bundle that does not exist: the app says so and exits with 1.
        for (const QStringList& args : {QStringList{QStringLiteral("--screenshot"), shot, nowhere, QStringLiteral("--bundle"), QStringLiteral("x")},
                                        QStringList{QStringLiteral("--profile"), QStringLiteral("1"), nowhere, QStringLiteral("--bundle"), QStringLiteral("x")},
                                        QStringList{QStringLiteral("--record"), QDir(settings.path()).filePath(QStringLiteral("f")), nowhere,
                                                    QStringLiteral("--bundle"), QStringLiteral("x")}}) {
            AppRun run = runApp(args, settings.path());
            QVERIFY2(run.finished && run.status == QProcess::NormalExit && run.exitCode == 1,
                     qPrintable(args.join(u' ') + u'\n' + describe(run)));
            QVERIFY(run.err.contains(QStringLiteral("could not open")));
        }
    }
};

QTEST_MAIN(TestIntegration)
#include "tst_integration.moc"
