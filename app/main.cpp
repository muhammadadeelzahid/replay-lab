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

#include <cstdio>
#include <memory>
#include <optional>

namespace {

/// `--screenshot`: once the bundle (and its log index) or the folder is ready, save a picture of
/// the window and quit. Used for the README images and to check the layout without a display.
void scheduleScreenshot(abrviz::MainWindow& window, const QString& file, bool waitForBundle,
                        std::optional<double> time)
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
    } else {
        QObject::connect(&window, &abrviz::MainWindow::folderScanned, &window, take);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("abr-viz"));
    QApplication::setApplicationVersion(abrviz::versionString());
    QApplication::setOrganizationName(QStringLiteral("abr-viz"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Viewer for ns-3 adaptive bitrate run bundles"));
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
    parser.addOptions({bundleOption, timeOption, sizeOption, screenshotOption, engineOption});
    parser.process(app);

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
    // A screenshot run lays the window out and paints it without putting it on the screen.
    if (parser.isSet(screenshotOption))
        window.setAttribute(Qt::WA_DontShowOnScreen);
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
    if (parser.isSet(screenshotOption)) {
        if (path.isEmpty())
            parser.showHelp(1);
        scheduleScreenshot(window, parser.value(screenshotOption), !bundle.isEmpty(), time);
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
