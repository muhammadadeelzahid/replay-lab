#include "main_window.h"

#include "abrviz/core/version.h"
#include "abrviz/media/engine_support.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>

namespace abrviz {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("abr-viz"));
    resize(1280, 800);

    // Owned by the window through Qt's parent-child tree.
    auto* placeholder = new QLabel(tr("Open a bundle folder to begin."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    createMenus();

    const auto engines = media::engineSupport();
    const QString engineText = engines.gstreamer
        ? engines.gstreamerVersion
        : tr("GStreamer engine not built");
    statusBar()->showMessage(
        tr("abr-viz %1 · %2").arg(versionString(), engineText));
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
}

} // namespace abrviz
