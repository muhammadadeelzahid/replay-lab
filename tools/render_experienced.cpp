/**
 * @file
 * @brief render_experienced: renders the video a run's viewer saw.
 *
 * Every played segment is rendered at its played rung, stalls as frozen frames, cut off where
 * playback stopped, with the same RenderQueue the viewer uses.
 *
 * Usage:
 *
 *     render_experienced [--output file.mp4] [--cache dir] [--threads n] [--ffmpeg path] <bundle>
 *
 * Prints progress, the number of ffmpeg processes that ran at once, the time taken, and the
 * duration of the result compared with the replay time of the PlaybackClock.
 */

#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/core/render/render_queue.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QTextStream>

#include <csignal>

namespace {

QFuture<abrviz::RenderResult>* gRunning = nullptr;  ///< the render SIGINT cancels; null when idle

/**
 * @brief SIGINT handler: cancels the running render.
 *
 * Only sets a flag inside `QFutureInterface`; the job notices within 100 ms and the tool exits
 * with 130.
 */
extern "C" void onInterrupt(int)
{
    // Only sets a flag inside QFutureInterface; the job notices within 100 ms.
    if (gRunning)
        gRunning->cancel();
}

} // namespace

/**
 * @brief Renders the experienced video of one bundle and prints what happened.
 *
 * @param argc Argument count.
 * @param argv Options and the bundle folder (see the usage above).
 * @return 0 on success; 1 if the bundle cannot be loaded, the render fails or the copy to
 *         `--output` fails; 2 for wrong arguments; 130 if cancelled.
 */
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    // The app's names, so that the tool and the app share the render cache.
    QCoreApplication::setApplicationName(QStringLiteral("ReplayLab"));
    QCoreApplication::setOrganizationName(QStringLiteral("ReplayLab"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("muhammadadeelzahid.github.io"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Render the experienced video of a run"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("bundle"), QStringLiteral("Run bundle folder."));
    const QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                          QStringLiteral("Copy the result to this file."),
                                          QStringLiteral("file"));
    const QCommandLineOption cacheOption(QStringLiteral("cache"),
                                         QStringLiteral("Cache folder (default: the viewer's)."),
                                         QStringLiteral("dir"));
    const QCommandLineOption threadsOption(QStringLiteral("threads"),
                                           QStringLiteral("Parallel ffmpeg processes (default: one per core)."),
                                           QStringLiteral("n"));
    const QCommandLineOption ffmpegOption(QStringLiteral("ffmpeg"),
                                          QStringLiteral("ffmpeg program to run."),
                                          QStringLiteral("path"), QStringLiteral("ffmpeg"));
    parser.addOptions({outputOption, cacheOption, threadsOption, ffmpegOption});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(2);

    const abrviz::LoadResult loaded = abrviz::loadBundleSync(parser.positionalArguments().first());
    if (!loaded.run) {
        err << "could not load the bundle:\n  " << loaded.errors.join(QStringLiteral("\n  ")) << '\n';
        return 1;
    }
    const abrviz::PlaybackClock clock(*loaded.run);

    abrviz::RenderQueue::Options options;
    options.cacheDirectory = parser.value(cacheOption);
    options.ffmpeg = parser.value(ffmpegOption);
    options.threads = parser.value(threadsOption).toInt();
    abrviz::RenderQueue queue(options);
    out << loaded.run->info.runId << ": rendering with up to " << queue.threadCount()
        << " ffmpeg processes\n";
    out.flush();

    QFuture<abrviz::RenderResult> future = queue.enqueue({loaded.run});
    gRunning = &future;
    std::signal(SIGINT, onInterrupt);

    QFutureWatcher<abrviz::RenderResult> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::progressValueChanged, [&](int value) {
        out << "\r  " << value << " / " << future.progressMaximum() << Qt::flush;
    });
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &app, &QCoreApplication::quit);
    watcher.setFuture(future);
    if (!future.isFinished())
        QCoreApplication::exec();
    std::signal(SIGINT, SIG_DFL);
    gRunning = nullptr;
    out << '\n';

    if (future.isCanceled() || future.resultCount() == 0) {
        err << "cancelled\n";
        return 130;
    }
    const abrviz::RenderResult result = future.result();
    if (!result.ok()) {
        err << "failed: " << result.error << '\n';
        return 1;
    }
    const auto duration = abrviz::readMp4Duration(result.path);
    out << "  file           " << result.path << (result.fromCache ? " (from the cache)" : "") << '\n';
    out << "  parts          " << result.parts << '\n';
    if (!result.fromCache) {
        out << "  ffmpeg at once " << result.peakConcurrency << '\n';
        out << "  time           " << QString::number(result.seconds, 'f', 1) << " s\n";
    }
    out << "  duration       " << (duration ? QString::number(*duration, 'f', 3) : QStringLiteral("?"))
        << " s (replay time " << QString::number(clock.replayDuration(), 'f', 3) << " s)\n";

    if (parser.isSet(outputOption)) {
        const QString target = parser.value(outputOption);
        QFile::remove(target);
        if (!QFile::copy(result.path, target)) {
            err << "could not copy to " << target << '\n';
            return 1;
        }
        out << "  copied to      " << target << '\n';
    }
    return 0;
}
