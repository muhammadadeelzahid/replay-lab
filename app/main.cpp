#include "main_window.h"

#include "abrviz/core/version.h"

#include <QApplication>
#include <QCommandLineParser>

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
    parser.process(app);

    abrviz::MainWindow window;
    window.show();
    if (const QStringList paths = parser.positionalArguments(); !paths.isEmpty())
        window.openPath(paths.first());
    return QApplication::exec();
}
