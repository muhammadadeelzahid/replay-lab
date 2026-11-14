#include "main_window.h"

#include "charts_panel.h"
#include "library_view.h"
#include "log_console.h"
#include "segment_timeline.h"
#include "session.h"
#include "summary_panel.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_loader.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/logs/log_index.h"
#include "abrviz/core/util/future_guard.h"
#include "abrviz/core/version.h"
#include "abrviz/media/engine_support.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>

#include <tuple>

namespace abrviz {

struct MainWindow::Impl {
    // Widgets and watchers are owned by the window through Qt's parent-child tree.
    Session* session = nullptr;
    LibraryView* library = nullptr;
    SummaryPanel* summary = nullptr;
    SegmentTimeline* timeline = nullptr;
    ChartsPanel* charts = nullptr;
    LogConsole* logs = nullptr;
    QLabel* placeholder = nullptr;
    QLabel* versionLabel = nullptr;
    QProgressBar* progress = nullptr;
    QFutureWatcher<QList<BundleSummaryPtr>>* scanWatcher = nullptr;
    QFutureWatcher<LoadResult>* loadWatcher = nullptr;
    QFutureWatcher<LogIndexPtr>* logWatcher = nullptr;

    // Pending tasks. The guards cancel and wait when replaced or destroyed.
    FutureGuard<QList<BundleSummaryPtr>> scan;
    FutureGuard<LoadResult> load;
    FutureGuard<LogIndexPtr> logIndex;
    std::shared_ptr<const RunData> logIndexRun;  // the run the pending index is for
    QString scanningFolder;
    QString loadingBundle;
    bool shutDown = false;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , d_(std::make_unique<Impl>())
{
    setWindowTitle(QStringLiteral("abr-viz"));
    resize(1400, 900);

    // The session has no parent while the panels are created and is made a child of the window
    // afterwards. Qt deletes children in the order they were added, so the session, which the
    // panels point to, is deleted after them.
    auto session = std::make_unique<Session>();
    d_->session = session.get();

    d_->placeholder = new QLabel(tr("Open a folder of bundles to begin."), this);
    d_->placeholder->setAlignment(Qt::AlignCenter);
    d_->placeholder->setWordWrap(true);
    setCentralWidget(d_->placeholder);

    createDocks();
    createMenus();
    session.release()->setParent(this);

    d_->versionLabel = new QLabel(this);
    const auto engines = media::engineSupport();
    const QString engineText = engines.gstreamer ? engines.gstreamerVersion
                                                 : tr("GStreamer engine not built");
    d_->versionLabel->setText(tr("abr-viz %1 · %2").arg(versionString(), engineText));
    statusBar()->addPermanentWidget(d_->versionLabel);

    d_->progress = new QProgressBar(this);
    d_->progress->setMaximumWidth(220);
    d_->progress->setTextVisible(false);
    d_->progress->hide();
    statusBar()->addPermanentWidget(d_->progress);

    d_->scanWatcher = new QFutureWatcher<QList<BundleSummaryPtr>>(this);
    connect(d_->scanWatcher, &QFutureWatcherBase::finished, this, &MainWindow::scanFinished);

    d_->loadWatcher = new QFutureWatcher<LoadResult>(this);
    connect(d_->loadWatcher, &QFutureWatcherBase::finished, this, &MainWindow::loadFinished);
    connect(d_->loadWatcher, &QFutureWatcherBase::progressRangeChanged, d_->progress,
            &QProgressBar::setRange);
    connect(d_->loadWatcher, &QFutureWatcherBase::progressValueChanged, d_->progress,
            &QProgressBar::setValue);

    d_->logWatcher = new QFutureWatcher<LogIndexPtr>(this);
    connect(d_->logWatcher, &QFutureWatcherBase::finished, this, &MainWindow::logIndexFinished);

    connect(d_->library, &LibraryView::openRequested, this, &MainWindow::openBundle);
    connect(d_->session, &Session::runChanged, this, [this] {
        const auto& run = d_->session->run();
        setWindowTitle(run ? QStringLiteral("%1 - abr-viz").arg(run->info.runId)
                           : QStringLiteral("abr-viz"));
        d_->placeholder->setText(run ? tr("%1\n\nThe replay player is added in a later step.")
                                           .arg(run->info.runId)
                                     : tr("Open a folder of bundles to begin."));
    });
}

MainWindow::~MainWindow()
{
    // Tasks first; then `d_` and, after it, the child widgets.
    shutdown();
}

void MainWindow::shutdown()
{
    if (d_->shutDown)
        return;
    d_->shutDown = true;
    // No result may arrive once shutdown has started.
    d_->scanWatcher->disconnect(this);
    d_->loadWatcher->disconnect(this);
    d_->logWatcher->disconnect(this);
    d_->scan.reset();
    d_->load.reset();
    d_->logIndex.reset();
}

Session* MainWindow::session() const { return d_->session; }
LibraryView* MainWindow::library() const { return d_->library; }
SegmentTimeline* MainWindow::timeline() const { return d_->timeline; }
ChartsPanel* MainWindow::charts() const { return d_->charts; }
LogConsole* MainWindow::logs() const { return d_->logs; }
SummaryPanel* MainWindow::summary() const { return d_->summary; }

bool MainWindow::isBusy() const
{
    return d_->scan.isRunning() || d_->load.isRunning() || d_->logIndex.isRunning();
}

void MainWindow::createDocks()
{
    auto addDock = [this](const QString& title, const QString& name, QWidget* widget,
                          Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(name);
        dock->setWidget(widget);
        addDockWidget(area, dock);
        return dock;
    };

    d_->library = new LibraryView(this);
    d_->summary = new SummaryPanel(d_->session, this);
    d_->timeline = new SegmentTimeline(d_->session, this);
    d_->charts = new ChartsPanel(d_->session, this);
    d_->logs = new LogConsole(d_->session, this);

    auto* library = addDock(tr("Library"), QStringLiteral("libraryDock"), d_->library,
                            Qt::LeftDockWidgetArea);
    addDock(tr("Run"), QStringLiteral("summaryDock"), d_->summary, Qt::LeftDockWidgetArea);
    auto* timeline = addDock(tr("Timeline"), QStringLiteral("timelineDock"), d_->timeline,
                             Qt::BottomDockWidgetArea);
    auto* logs = addDock(tr("Log"), QStringLiteral("logDock"), d_->logs, Qt::BottomDockWidgetArea);
    splitDockWidget(timeline, logs, Qt::Vertical);
    auto* charts = addDock(tr("Charts"), QStringLiteral("chartsDock"), d_->charts,
                           Qt::RightDockWidgetArea);
    resizeDocks({library, charts}, {520, 620}, Qt::Horizontal);
    resizeDocks({logs}, {220}, Qt::Vertical);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openFolderAction = fileMenu->addAction(tr("Open &Folder..."));
    openFolderAction->setShortcut(QKeySequence::Open);
    connect(openFolderAction, &QAction::triggered, this, &MainWindow::chooseFolder);
    auto* openBundleAction = fileMenu->addAction(tr("Open &Bundle..."));
    openBundleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openBundleAction, &QAction::triggered, this, &MainWindow::chooseBundle);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    for (auto* dock : findChildren<QDockWidget*>())
        viewMenu->addAction(dock->toggleViewAction());

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About abr-viz"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About abr-viz"),
                           tr("abr-viz %1\nViewer for ns-3 adaptive bitrate run bundles.")
                               .arg(versionString()));
    });
}

void MainWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder of Bundles"));
    if (!folder.isEmpty())
        openFolder(folder);
}

void MainWindow::chooseBundle()
{
    const QString bundle = QFileDialog::getExistingDirectory(this, tr("Open Bundle"));
    if (!bundle.isEmpty())
        openPath(bundle);
}

void MainWindow::openPath(const QString& path)
{
    openFolder(path);
    if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("run.json"))))
        openBundle(path);
}

void MainWindow::openFolder(const QString& folder)
{
    if (d_->shutDown)
        return;
    d_->scanningFolder = QDir(folder).absolutePath();
    d_->library->setScanning(d_->scanningFolder);
    auto future = scanLibrary(d_->scanningFolder);
    d_->scan.reset(future);
    d_->scanWatcher->setFuture(future);
    updateStatus();
}

void MainWindow::openBundle(const QString& directory)
{
    if (d_->shutDown)
        return;
    d_->loadingBundle = QDir(directory).absolutePath();
    auto future = loadBundle(d_->loadingBundle);
    d_->load.reset(future);
    d_->progress->setRange(0, 0);
    d_->progress->show();
    d_->loadWatcher->setFuture(future);
    updateStatus();
}

void MainWindow::scanFinished()
{
    const auto future = d_->scanWatcher->future();
    // Finished: stop guarding it. `reset()` would cancel it, which discards the result.
    std::ignore = d_->scan.release();
    if (future.isCanceled() || future.resultCount() == 0) {
        updateStatus();
        return;
    }
    d_->library->setBundles(future.result(), d_->scanningFolder);
    if (d_->session->run())
        d_->library->setOpenBundle(d_->session->run()->info.bundleDirectory);
    updateStatus();
    emit folderScanned(d_->scanningFolder);
}

void MainWindow::loadFinished()
{
    const auto future = d_->loadWatcher->future();
    std::ignore = d_->load.release();
    d_->progress->hide();
    if (future.isCanceled() || future.resultCount() == 0) {
        updateStatus();
        return;
    }
    LoadResult result = future.result();
    const bool ok = result.run != nullptr;
    d_->session->setRun(std::move(result.run), std::move(result.errors));
    d_->library->setOpenBundle(d_->loadingBundle);
    if (ok)
        startLogIndex();
    updateStatus();
    if (!ok)
        statusBar()->showMessage(tr("Could not open %1").arg(QDir(d_->loadingBundle).dirName()));
    emit bundleOpened(d_->loadingBundle, ok);
}

void MainWindow::startLogIndex()
{
    const auto& run = d_->session->run();
    d_->logIndexRun = run;
    const QString directory = run->info.bundleDirectory;
    auto future = buildLogIndex(simLogPath(directory), policyLogPath(directory), run->info.nodeId);
    d_->logIndex.reset(future);
    d_->logWatcher->setFuture(future);
}

void MainWindow::logIndexFinished()
{
    const auto future = d_->logWatcher->future();
    std::ignore = d_->logIndex.release();
    const auto run = std::exchange(d_->logIndexRun, nullptr);
    if (future.isCanceled() || future.resultCount() == 0 || !run)
        return;
    // The session ignores an index for a run that is no longer open.
    d_->session->setLogIndex(future.result(), run.get());
    emit logIndexReady();
}

void MainWindow::updateStatus()
{
    if (d_->load.isRunning())
        statusBar()->showMessage(tr("Loading %1 ...").arg(QDir(d_->loadingBundle).dirName()));
    else if (d_->scan.isRunning())
        statusBar()->showMessage(tr("Reading %1 ...").arg(QDir(d_->scanningFolder).dirName()));
    else
        statusBar()->clearMessage();
}

} // namespace abrviz
