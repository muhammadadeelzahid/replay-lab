/**
 * @file
 * @brief The program's entry point: command-line options, the main window, and the
 *        `--screenshot` and `--profile` modes.
 */

#include "background_mode.h"
#include "branding.h"
#include "icons.h"
#include "main_window.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>

#include <QVideoSink>

#ifdef Q_OS_WIN
#define NOMINMAX  // windows.h would otherwise turn std::min and std::max into macros
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

namespace {

/**
 * @brief `--screenshot`: once the bundle (and its log index) or the folder is ready, saves a
 *        picture of the window and quits.
 *
 * Used for the README images and to check the layout without a display. Before grabbing it
 * waits (at most 10 s) until the engine shows a picture or has nothing to show, then 600 ms
 * more for the panels to repaint and lay out. Prints "saved <file>" or "could not save <file>"
 * and exits the event loop with 0 or 1; exits with 1 if the bundle cannot be opened.
 *
 * @param window The main window to grab; must outlive the event loop.
 * @param file The image file to write; the format follows its extension.
 * @param waitForBundle Wait for logIndexReady() (a bundle is being opened).
 * @param waitForFolder Wait for folderScanned() (only a folder is being opened); ignored
 *                      when `waitForBundle` is set.
 * @param time If set, the simulation time (seconds) to move the cursor to before grabbing.
 *
 * @note With neither wait flag, the welcome screen is grabbed as soon as the event loop runs.
 */
void scheduleScreenshot(abrviz::MainWindow& window, const QString& file, bool waitForBundle,
                        bool waitForFolder, std::optional<double> time)
{
    auto take = [&window, file, time] {
        if (time)
            window.session()->cursor()->setTime(*time);
        // Wait (at most 10 s) until the engine shows a picture or has nothing to show, then let
        // the panels repaint and lay out before grabbing.
        auto* poll = new QTimer(&window);
        auto started = std::make_shared<QElapsedTimer>();
        started->start();
        QObject::connect(poll, &QTimer::timeout, &window, [&window, file, poll, started] {
            const auto* engine = window.player()->engine();
            const bool waiting = engine && engine->state() == abrviz::ReplaySource::State::Preparing
                && engine->isReady();
            if (waiting && started->elapsed() < 10000)
                return;
            poll->deleteLater();
            QTimer::singleShot(600, &window, [&window, file] {
                const bool saved = window.grab().save(file);
                std::fprintf(saved ? stdout : stderr, "%s %s\n",
                             saved ? "saved" : "could not save", qPrintable(file));
                QApplication::exit(saved ? 0 : 1);
            });
        });
        poll->start(50);
    };
    if (waitForBundle) {
        QObject::connect(&window, &abrviz::MainWindow::logIndexReady, &window, take);
        QObject::connect(&window, &abrviz::MainWindow::bundleOpened, &window,
                         [](const QString& directory, bool ok) {
                             if (!ok) {
                                 std::fprintf(stderr, "could not open %s\n", qPrintable(directory));
                                 QApplication::exit(1);
                             }
                         });
    } else if (waitForFolder) {
        QObject::connect(&window, &abrviz::MainWindow::folderScanned, &window, take);
    } else {
        QTimer::singleShot(0, &window, take);  // the welcome screen
    }
}

/**
 * @brief The CPU time the process has used so far.
 *
 * @return User plus system time, in seconds.
 */
double cpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto seconds = [](const FILETIME& t) {
        const auto ticks = (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        return static_cast<double>(ticks) / 1e7;  // 100 ns units
    };
    return seconds(kernel) + seconds(user);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto seconds = [](const timeval& t) {
        return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6;
    };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
#endif
}

/**
 * @brief The process's peak resident memory.
 *
 * @note getrusage() reports ru_maxrss in bytes on macOS and in kilobytes on Linux; Windows
 *       reports the peak working set in bytes. All are converted.
 *
 * @return The maximum resident set size so far, in MiB.
 */
double peakMemoryMb()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef Q_OS_MACOS
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // kilobytes
#endif
#endif
}

/**
 * @brief Describes the segment shown at a replay time, for the `--profile` gap list.
 *
 * @param window The main window, whose session holds the run and the playback clock.
 * @param replay The replay time, in seconds.
 * @return "segment N at P%, rung A -> B" (A is the rung of the segment shown before it in
 *         playback order, -1 if none), with ", stalled" appended during a stall; "before
 *         playback" without a run or before the first segment.
 */
QString describeReplayTime(abrviz::MainWindow& window, double replay)
{
    const auto& run = window.session()->run();
    const auto position = window.session()->clock().positionAtReplay(replay);
    if (!run || position.segment < 0)
        return QStringLiteral("before playback");
    const auto rungOf = [&run](int segment) {
        const auto it = std::ranges::find(run->segments, segment, &abrviz::Segment::index);
        return it == run->segments.end() ? -1 : it->playedRung;
    };
    // The segment shown before this one in playback order.
    int previous = -1;
    for (const auto& piece : window.session()->clock().pieces()) {
        if (piece.kind != abrviz::PlaybackClock::Piece::Kind::Segment)
            continue;
        if (piece.segment == position.segment)
            break;
        previous = piece.segment;
    }
    return QStringLiteral("segment %1 at %2%, rung %3 -> %4%5")
        .arg(position.segment)
        .arg(position.fraction * 100.0, 0, 'f', 0)
        .arg(rungOf(previous))
        .arg(rungOf(position.segment))
        .arg(position.stalled ? QStringLiteral(", stalled") : QString());
}

/**
 * @brief `--profile`: once the bundle and its log index are ready, plays part of the replay and
 *        prints load, render and playback numbers, then quits.
 *
 * Uses the current engine, rendering first if the engine needs it. Playback stops after
 * `seconds` or earlier when the replay ends. The report covers the load, log-index and
 * engine-ready times, the replay time played, the frames shown and decoded, CPU use, peak
 * memory, and every gap between frames longer than 150 ms. Exits the event loop with 0, or
 * with 1 if the bundle cannot be opened or the engine fails.
 *
 * @param window The main window; must outlive the event loop.
 * @param seconds How much of the replay to play, in wall-clock seconds.
 * @param time If set, the simulation time (seconds) to move the cursor to before playing.
 */
void scheduleProfile(abrviz::MainWindow& window, double seconds, std::optional<double> time)
{
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto loadMs = std::make_shared<qint64>(0);
    QObject::connect(&window, &abrviz::MainWindow::bundleOpened, &window,
                     [clock, loadMs](const QString& directory, bool ok) {
                         *loadMs = clock->elapsed();
                         if (!ok) {
                             std::fprintf(stderr, "could not open %s\n", qPrintable(directory));
                             QApplication::exit(1);
                         }
                     });
    QObject::connect(&window, &abrviz::MainWindow::logIndexReady, &window, [&window, clock, loadMs, seconds, time] {
        const qint64 indexMs = clock->elapsed();
        if (time)
            window.session()->cursor()->setTime(*time);
        auto* player = window.player();
        auto renderClock = std::make_shared<QElapsedTimer>();
        renderClock->start();
        if (!player->engine()->isReady())
            player->prepare();
        auto* poll = new QTimer(&window);
        QObject::connect(poll, &QTimer::timeout, &window, [&window, player, poll, renderClock, indexMs, loadMs, seconds] {
            const auto* engine = player->engine();
            if (engine->state() == abrviz::ReplaySource::State::Error) {
                std::fprintf(stderr, "engine error\n");
                QApplication::exit(1);
                return;
            }
            if (!engine->isReady() || engine->state() != abrviz::ReplaySource::State::Paused)
                return;
            poll->deleteLater();
            const qint64 readyMs = renderClock->elapsed();
            auto frames = std::make_shared<int>(0);
            // Gaps between frames longer than 150 ms, with the replay time they ended at.
            auto frameClock = std::make_shared<QElapsedTimer>();
            auto gaps = std::make_shared<QStringList>();
            QObject::connect(player->videoSink(), &QVideoSink::videoFrameChanged, &window,
                             [&window, frames, frameClock, gaps, player] {
                                 ++*frames;
                                 if (frameClock->isValid() && frameClock->elapsed() > 150) {
                                     const double replay = player->engine()->position();
                                     *gaps << QStringLiteral("%1 ms at replay %2 s (%3)")
                                                  .arg(frameClock->elapsed())
                                                  .arg(replay, 0, 'f', 2)
                                                  .arg(describeReplayTime(window, replay));
                                 }
                                 frameClock->start();
                             });
            const qint64 decodedBefore = engine->property("framesDecoded").toLongLong();
            const double cpuBefore = cpuSeconds();
            const double startPosition = engine->position();
            auto playClock = std::make_shared<QElapsedTimer>();
            playClock->start();
            player->play();
            auto finished = std::make_shared<bool>(false);
            const auto finish = [&window, player, frames, gaps, decodedBefore, cpuBefore, playClock, startPosition, indexMs, loadMs, readyMs, finished] {
                if (std::exchange(*finished, true))
                    return;
                const double wall = static_cast<double>(playClock->elapsed()) / 1000.0;
                const double cpu = cpuSeconds() - cpuBefore;
                const double end = player->engine()->position();
                player->pause();
                const auto& run = window.session()->run();
                std::printf("run              %s\n", qPrintable(run->info.runId));
                std::printf("engine           %s\n", qPrintable(player->engine()->name()));
                std::printf("bundle loaded    %lld ms\n", static_cast<long long>(*loadMs));
                std::printf("log index ready  %lld ms\n", static_cast<long long>(indexMs));
                std::printf("engine ready     %lld ms after that\n", static_cast<long long>(readyMs));
                std::printf("played           %.2f s of replay (%.2f to %.2f) in %.2f s\n",
                            end - startPosition, startPosition, end, wall);
                std::printf("frames shown     %d (%.1f per second; the video has %.2f)\n", *frames,
                            *frames / wall, run->video.frameRate);
                if (player->engine()->property("framesDecoded").isValid())
                    std::printf("frames decoded   %lld\n",
                                player->engine()->property("framesDecoded").toLongLong() - decodedBefore);
                std::printf("CPU while playing %.0f %% of one core\n", 100.0 * cpu / wall);
                std::printf("peak memory      %.0f MB\n", peakMemoryMb());
                std::printf("gaps over 150 ms %lld%s%s\n", static_cast<long long>(gaps->size()),
                            gaps->isEmpty() ? "" : ": ", qPrintable(gaps->join(QStringLiteral(", "))));
                QApplication::exit(0);
            };
            // Stops after `seconds`, or earlier when the replay ends.
            QTimer::singleShot(static_cast<int>(seconds * 1000.0), &window, finish);
            QObject::connect(player, &abrviz::ReplayPlayer::stateChanged, &window,
                             [finish](abrviz::ReplaySource::State state) {
                                 if (state == abrviz::ReplaySource::State::Ended)
                                     finish();
                             });
        });
        poll->start(20);
    });
}

} // namespace

/**
 * @brief Starts the application.
 *
 * Parses the command line (`[path]`, `--bundle`, `--time`, `--size`, `--screenshot`,
 * `--engine`, `--page`, `--profile`), creates the main window and opens the path: a folder is
 * listed, and a bundle (the path itself if it holds a `run.json`, or `--bundle` inside it) is
 * opened. `--screenshot` and `--profile` run as a background-only application and paint the
 * window without putting it on the screen.
 *
 * @param argc The argument count.
 * @param argv The arguments.
 * @return The event loop's exit code; 2 for an engine that is not built or an unknown page;
 *         1 if `--profile` is given without a bundle.
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Stream Inspector"));
    QApplication::setApplicationDisplayName(abrviz::appDisplayName());
    QApplication::setApplicationVersion(abrviz::versionString());
    QApplication::setOrganizationName(QStringLiteral("Stream Inspector"));
    QApplication::setOrganizationDomain(QStringLiteral("muhammadadeelzahid.github.io"));
    QApplication::setWindowIcon(abrviz::appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Stream Inspector: replays ns-3 adaptive bitrate run bundles"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("A folder of bundles, or one bundle to open."),
                                 QStringLiteral("[path]"));
    const QCommandLineOption bundleOption(
        QStringLiteral("bundle"), QStringLiteral("Bundle in the folder to open."),
        QStringLiteral("name"));
    const QCommandLineOption timeOption(
        QStringLiteral("time"), QStringLiteral("Simulation time to move the cursor to."),
        QStringLiteral("seconds"));
    const QCommandLineOption sizeOption(
        QStringLiteral("size"), QStringLiteral("Window size, for example 1600x1000."),
        QStringLiteral("WxH"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a picture of the window once the run is loaded, then quit."),
        QStringLiteral("file"));
    const QCommandLineOption engineOption(
        QStringLiteral("engine"),
        QStringLiteral("Replay engine: gstreamer or prerendered (overrides the saved choice)."),
        QStringLiteral("name"));
    const QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("Page to show: library, replay, analysis, logs or report."),
        QStringLiteral("name"));
    const QCommandLineOption profileOption(
        QStringLiteral("profile"),
        QStringLiteral("Open the bundle, play this many seconds of the replay, print timings and quit."),
        QStringLiteral("seconds"));
    parser.addOptions({bundleOption, timeOption, sizeOption, screenshotOption, engineOption,
                       pageOption, profileOption});
    parser.process(app);
    if (parser.isSet(screenshotOption) || parser.isSet(profileOption))
        abrviz::runInBackground();

    abrviz::MainWindow window;
    if (parser.isSet(engineOption)) {
        const auto engine = abrviz::engineFromKey(parser.value(engineOption));
        if (!engine || !window.setEngine(*engine)) {
            std::fprintf(stderr, "engine '%s' is not available in this build\n",
                         qPrintable(parser.value(engineOption)));
            return 2;
        }
    }
    if (parser.isSet(sizeOption)) {
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2)
            window.resize(parts[0].toInt(), parts[1].toInt());
    }
    // The page asked for is shown once the run is open (opening a run shows the replay).
    std::optional<abrviz::MainWindow::Page> page;
    if (parser.isSet(pageOption)) {
        for (int i = 0; i < abrviz::MainWindow::kPageCount; ++i) {
            const auto candidate = static_cast<abrviz::MainWindow::Page>(i);
            if (abrviz::MainWindow::pageName(candidate).compare(parser.value(pageOption), Qt::CaseInsensitive) == 0)
                page = candidate;
        }
        if (!page) {
            std::fprintf(stderr, "unknown page '%s'\n", qPrintable(parser.value(pageOption)));
            return 2;
        }
        QObject::connect(&window, &abrviz::MainWindow::bundleOpened, &window,
                         [&window, page] { window.showPage(*page); });
        window.showPage(*page);
    }
    // Screenshot and profile runs lay the window out and paint it without putting it on the
    // screen.
    // They run as background-only apps (never activated, so they cannot take the keyboard focus
    // from the app in use) and paint the window without putting it on the screen.
    if (parser.isSet(screenshotOption) || parser.isSet(profileOption)) {
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
    }
    window.show();

    const QStringList paths = parser.positionalArguments();
    const QString path = paths.isEmpty() ? QString() : paths.first();
    QString bundle;
    if (parser.isSet(bundleOption) && !path.isEmpty())
        bundle = QDir(path).filePath(parser.value(bundleOption));
    else if (!path.isEmpty() && QFileInfo::exists(QDir(path).filePath(QStringLiteral("run.json"))))
        bundle = path;

    std::optional<double> time;
    if (parser.isSet(timeOption))
        time = parser.value(timeOption).toDouble();
    if (parser.isSet(profileOption)) {
        if (bundle.isEmpty())
            parser.showHelp(1);
        scheduleProfile(window, parser.value(profileOption).toDouble(), time);
    } else if (parser.isSet(screenshotOption)) {
        scheduleScreenshot(window, parser.value(screenshotOption), !bundle.isEmpty(), !path.isEmpty(), time);
    } else if (time) {
        QObject::connect(window.session(), &abrviz::Session::runChanged, &window, [&window, time] {
            window.session()->cursor()->setTime(*time);
        });
    }

    if (!path.isEmpty()) {
        window.openFolder(path);
        if (!bundle.isEmpty())
            window.openBundle(bundle);
    }
    return QApplication::exec();
}
