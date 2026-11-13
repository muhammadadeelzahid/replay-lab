#include "main_window.h"

#include "abrviz/core/version.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("abr-viz"));
    QApplication::setApplicationVersion(abrviz::versionString());
    QApplication::setOrganizationName(QStringLiteral("abr-viz"));

    abrviz::MainWindow window;
    window.show();
    return QApplication::exec();
}
