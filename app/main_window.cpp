#include "main_window.h"

#include "charts_panel.h"
#include "icons.h"
#include "library_view.h"
#include "log_console.h"
#include "moment_panel.h"
#include "replay_player.h"
#include "report_view.h"
#include "segment_timeline.h"
#include "session.h"
#include "format.h"
#include "summary_panel.h"
#include "time_cursor.h"
#include "transport_bar.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_loader.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/logs/log_index.h"
#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/util/future_guard.h"
#include "abrviz/core/version.h"
#include "abrviz/media/engine_support.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <array>
#include <tuple>

namespace abrviz {

namespace {

const QString kEngineSetting = QStringLiteral("replay/engine");
const QString kRecentSetting = QStringLiteral("library/recentFolders");
const QString kGeometrySetting = QStringLiteral("window/geometry");
const QString kOverlaySetting = QStringLiteral("replay/overlay");
constexpr int kRecentCount = 6;
constexpr double kStepSeconds = 5.0;

Glyph pageGlyph(MainWindow::Page page)
{
    switch (page) {
    case MainWindow::Page::Library: return Glyph::Library;
    case MainWindow::Page::Replay: return Glyph::Replay;
    case MainWindow::Page::Analysis: return Glyph::Analysis;
    case MainWindow::Page::Logs: return Glyph::Logs;
    case MainWindow::Page::Report: return Glyph::Report;
    }
    return Glyph::Library;
}

/// A page with a title over its content.
QWidget* titledPage(const QString& title, const QString& subtitle, QWidget* content, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* heading = new QLabel(title, page);
    QFont font = heading->font();
    font.setPointSizeF(font.pointSizeF() * 1.5);
    font.setBold(true);
    heading->setFont(font);
    auto* note = new QLabel(subtitle, page);
    note->setForegroundRole(QPalette::PlaceholderText);
    note->setWordWrap(true);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 10);
    layout->setSpacing(4);
    layout->addWidget(heading);
    layout->addWidget(note);
    layout->addSpacing(8);
    layout->addWidget(content, 1);
    return page;
}

} // namespace

struct MainWindow::Impl {
    // Renders the experienced video for the pre-rendered engine. Destroyed with Impl, after
    // shutdown() has destroyed the engine that uses it and before the child widgets.
    std::unique_ptr<RenderQueue> renderQueue;

    // Widgets and watchers are owned by the window through Qt's parent-child tree.
    Session* session = nullptr;
    LibraryView* library = nullptr;
    SummaryPanel* summary = nullptr;
    ReportView* report = nullptr;
    SegmentTimeline* timeline = nullptr;
    ChartsPanel* charts = nullptr;
    LogConsole* logs = nullptr;
    ReplayPlayer* player = nullptr;
    MomentPanel* moment = nullptr;
    TransportBar* transport = nullptr;
    QListWidget* sidebar = nullptr;
    QStackedWidget* pages = nullptr;
    QComboBox* runPicker = nullptr;
    QComboBox* engineBox = nullptr;
    QToolBar* toolBar = nullptr;
    QMenu* recentMenu = nullptr;

    QAction* openFolderAction = nullptr;
    QAction* openBundleAction = nullptr;
    QAction* playAction = nullptr;
    QAction* stepBackAction = nullptr;
    QAction* stepForwardAction = nullptr;
    QAction* previousSegmentAction = nullptr;
    QAction* nextSegmentAction = nullptr;
    QAction* renderAction = nullptr;
    QAction* overlayAction = nullptr;
    QAction* fullScreenAction = nullptr;
    std::array<QAction*, kPageCount> pageActions{};
    QActionGroup* engineActions = nullptr;

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
    setWindowIcon(appIcon());
    resize(1440, 900);

    // The session has no parent while the panels are created and is made a child of the window
    // afterwards. Qt deletes children in the order they were added, so the session, which the
    // panels point to, is deleted after them.
    auto session = std::make_unique<Session>();
    d_->session = session.get();

    // ABRVIZ_RENDER_CACHE and ABRVIZ_FFMPEG override where rendered videos are kept and which
    // ffmpeg renders them (the tests use both).
    const auto environment = QProcessEnvironment::systemEnvironment();
    RenderQueue::Options renderOptions;
    renderOptions.cacheDirectory = environment.value(QStringLiteral("ABRVIZ_RENDER_CACHE"));
    renderOptions.ffmpeg = environment.value(QStringLiteral("ABRVIZ_FFMPEG"), QStringLiteral("ffmpeg"));
    d_->renderQueue = std::make_unique<RenderQueue>(renderOptions);

    d_->player = new ReplayPlayer(d_->session, d_->renderQueue.get(), this);
    createActions();
    createPages();
    createMenus();
    createToolBar();
    session.release()->setParent(this);

    d_->versionLabel = new QLabel(this);
    const auto engines = media::engineSupport();
    const QString engineText = engines.gstreamer ? engines.gstreamerVersion
                                                 : tr("GStreamer engine not built");
    d_->versionLabel->setText(tr("abr-viz %1 · %2").arg(versionString(), engineText));
    d_->versionLabel->setForegroundRole(QPalette::PlaceholderText);
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
    connect(d_->library, &LibraryView::openFolderRequested, this, &MainWindow::chooseFolder);
    connect(d_->library, &LibraryView::openBundleRequested, this, &MainWindow::chooseBundle);
    connect(d_->library, &LibraryView::recentFolderRequested, this, &MainWindow::openFolder);
    connect(d_->session, &Session::runChanged, this, [this] {
        const auto& run = d_->session->run();
        setWindowTitle(run ? QStringLiteral("%1 - abr-viz").arg(run->info.runId)
                           : QStringLiteral("abr-viz"));
        updateActions();
        updateRunPicker();
    });
    connect(d_->player, &ReplayPlayer::stateChanged, this, &MainWindow::updateActions);
    connect(d_->player, &ReplayPlayer::engineChanged, this, &MainWindow::updateActions);
    connect(d_->player, &ReplayPlayer::engineChanged, this, &MainWindow::updateEngineActions);
    connect(d_->player, &ReplayPlayer::engineFellBack, this, [this](const QString& reason) {
        statusBar()->showMessage(tr("GStreamer engine failed (%1); using the pre-rendered engine.")
                                     .arg(reason), 15000);
    });
    d_->player->setEngine(savedEngine());
    restoreSettings();
    applyStyle();
    showPage(Page::Library);
    updateActions();
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
    // The engine may run a job on the render queue, which `d_` destroys next.
    d_->player->releaseEngine();
}

Session* MainWindow::session() const { return d_->session; }
LibraryView* MainWindow::library() const { return d_->library; }
SegmentTimeline* MainWindow::timeline() const { return d_->timeline; }
ChartsPanel* MainWindow::charts() const { return d_->charts; }
ReplayPlayer* MainWindow::player() const { return d_->player; }
RenderQueue* MainWindow::renderQueue() const { return d_->renderQueue.get(); }
LogConsole* MainWindow::logs() const { return d_->logs; }
SummaryPanel* MainWindow::summary() const { return d_->summary; }
ReportView* MainWindow::report() const { return d_->report; }
TransportBar* MainWindow::transport() const { return d_->transport; }
MomentPanel* MainWindow::moment() const { return d_->moment; }

bool MainWindow::isBusy() const
{
    return d_->scan.isRunning() || d_->load.isRunning() || d_->logIndex.isRunning();
}

Engine MainWindow::savedEngine()
{
    const QSettings settings;
    const auto saved = engineFromKey(settings.value(kEngineSetting).toString());
    return saved && ReplaySourceFactory::isAvailable(*saved) ? *saved
                                                             : ReplaySourceFactory::defaultEngine();
}

bool MainWindow::setEngine(Engine engine, bool remember)
{
    if (!ReplaySourceFactory::isAvailable(engine))
        return false;
    if (d_->player->engineKind() != engine)
        d_->player->setEngine(engine);
    if (remember) {
        QSettings settings;
        settings.setValue(kEngineSetting, engineKey(engine));
    }
    updateEngineActions();
    return true;
}

void MainWindow::updateEngineActions()
{
    const auto current = d_->player->engineKind();
    if (d_->engineActions) {
        for (QAction* action : d_->engineActions->actions())
            action->setChecked(current && action->data().toString() == engineKey(*current));
    }
    if (d_->engineBox && current) {
        const QSignalBlocker block(d_->engineBox);
        d_->engineBox->setCurrentIndex(d_->engineBox->findData(engineKey(*current)));
    }
}

QString MainWindow::pageName(Page page)
{
    switch (page) {
    case Page::Library: return tr("Library");
    case Page::Replay: return tr("Replay");
    case Page::Analysis: return tr("Analysis");
    case Page::Logs: return tr("Logs");
    case Page::Report: return tr("Report");
    }
    return {};
}

void MainWindow::showPage(Page page)
{
    const int index = static_cast<int>(page);
    d_->pages->setCurrentIndex(index);
    {
        const QSignalBlocker block(d_->sidebar);
        d_->sidebar->setCurrentRow(index);
    }
    d_->pageActions[static_cast<std::size_t>(index)]->setChecked(true);
}

MainWindow::Page MainWindow::currentPage() const
{
    return static_cast<Page>(d_->pages->currentIndex());
}

void MainWindow::createPages()
{
    d_->library = new LibraryView(this);
    d_->summary = new SummaryPanel(d_->session, this);
    d_->report = new ReportView(d_->session, this);
    d_->timeline = new SegmentTimeline(d_->session, this);
    d_->charts = new ChartsPanel(d_->session, this);
    d_->logs = new LogConsole(d_->session, this);
    d_->moment = new MomentPanel(d_->session, d_->player, this);

    d_->pages = new QStackedWidget(this);
    d_->pages->addWidget(d_->library);

    auto* replay = new QWidget(this);
    auto* replayLayout = new QHBoxLayout(replay);
    replayLayout->setContentsMargins(0, 0, 0, 0);
    replayLayout->setSpacing(0);
    replayLayout->addWidget(d_->player, 1);
    auto* divider = new QFrame(replay);
    divider->setFrameShape(QFrame::VLine);
    divider->setObjectName(QStringLiteral("divider"));
    replayLayout->addWidget(divider);
    replayLayout->addWidget(d_->moment);
    d_->pages->addWidget(replay);

    d_->pages->addWidget(titledPage(tr("Analysis"),
                                    tr("Buffer, throughput, bitrate, network and decision delay over "
                                       "simulation time. Click or drag a chart to move the cursor."),
                                    d_->charts, this));
    d_->pages->addWidget(titledPage(tr("Logs"),
                                    tr("sim.log and the policy log, indexed. The table follows the cursor; "
                                       "selecting a line moves it."),
                                    d_->logs, this));
    auto* reportSplit = new QSplitter(Qt::Horizontal, this);
    reportSplit->addWidget(d_->report);
    reportSplit->addWidget(d_->summary);
    reportSplit->setStretchFactor(0, 3);
    reportSplit->setStretchFactor(1, 2);
    reportSplit->setChildrenCollapsible(false);
    d_->pages->addWidget(titledPage(tr("Report"),
                                    tr("Quality of experience for the open run, checked against the "
                                       "exporter's summary.json, and the run's settings and ladder."),
                                    reportSplit, this));

    d_->sidebar = new QListWidget(this);
    d_->sidebar->setObjectName(QStringLiteral("sidebar"));
    d_->sidebar->setFixedWidth(176);
    d_->sidebar->setIconSize(QSize(20, 20));
    d_->sidebar->setFrameShape(QFrame::NoFrame);
    d_->sidebar->setFocusPolicy(Qt::NoFocus);
    d_->sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int i = 0; i < kPageCount; ++i) {
        auto* item = new QListWidgetItem(pageName(static_cast<Page>(i)), d_->sidebar);
        item->setSizeHint(QSize(160, 38));
        item->setToolTip(d_->pageActions[static_cast<std::size_t>(i)]->shortcut().toString(QKeySequence::NativeText));
    }
    connect(d_->sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            showPage(static_cast<Page>(row));
    });

    d_->transport = new TransportBar(d_->session, d_->player, d_->timeline,
                                     {d_->previousSegmentAction, d_->stepBackAction, d_->playAction,
                                      d_->stepForwardAction, d_->nextSegmentAction},
                                     this);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(d_->pages, 1);
    contentLayout->addWidget(d_->transport);

    auto* central = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(d_->sidebar);
    centralLayout->addWidget(content, 1);
    setCentralWidget(central);
}

void MainWindow::createActions()
{
    auto make = [this](const QString& text, const QKeySequence& shortcut = {}) {
        auto* action = new QAction(text, this);
        action->setShortcut(shortcut);
        addAction(action);  // shortcuts work whichever page is shown
        return action;
    };
    d_->openFolderAction = make(tr("Open &Folder..."), QKeySequence::Open);
    d_->openFolderAction->setToolTip(tr("Open a folder of run bundles"));
    connect(d_->openFolderAction, &QAction::triggered, this, &MainWindow::chooseFolder);
    d_->openBundleAction = make(tr("Open &Bundle..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    d_->openBundleAction->setToolTip(tr("Open one run bundle"));
    connect(d_->openBundleAction, &QAction::triggered, this, &MainWindow::chooseBundle);

    d_->playAction = make(tr("&Play"), Qt::Key_Space);
    connect(d_->playAction, &QAction::triggered, d_->player, &ReplayPlayer::togglePlay);
    d_->stepBackAction = make(tr("&Back 5 Seconds"), QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(d_->stepBackAction, &QAction::triggered, this, [this] { stepReplay(-kStepSeconds); });
    d_->stepForwardAction = make(tr("&Forward 5 Seconds"), QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(d_->stepForwardAction, &QAction::triggered, this, [this] { stepReplay(kStepSeconds); });
    d_->previousSegmentAction = make(tr("P&revious Segment"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
    connect(d_->previousSegmentAction, &QAction::triggered, this, [this] { stepSegment(-1); });
    d_->nextSegmentAction = make(tr("&Next Segment"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
    connect(d_->nextSegmentAction, &QAction::triggered, this, [this] { stepSegment(1); });
    d_->renderAction = make(tr("&Render Video"), QKeySequence(Qt::CTRL | Qt::Key_R));
    d_->renderAction->setToolTip(tr("Render the experienced video for the pre-rendered engine"));
    connect(d_->renderAction, &QAction::triggered, d_->player, &ReplayPlayer::prepare);

    d_->overlayAction = make(tr("Show Video &Overlay"), QKeySequence(Qt::CTRL | Qt::Key_I));
    d_->overlayAction->setCheckable(true);
    connect(d_->overlayAction, &QAction::toggled, this, [this](bool on) {
        d_->player->setOverlayVisible(on);
        QSettings settings;
        settings.setValue(kOverlaySetting, on);
    });
    d_->fullScreenAction = make(tr("Enter &Full Screen"), QKeySequence::FullScreen);
    connect(d_->fullScreenAction, &QAction::triggered, this, [this] {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    });

    auto* pageGroup = new QActionGroup(this);
    for (int i = 0; i < kPageCount; ++i) {
        const auto page = static_cast<Page>(i);
        QAction* action = make(pageName(page), QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
        action->setCheckable(true);
        action->setActionGroup(pageGroup);
        connect(action, &QAction::triggered, this, [this, page] { showPage(page); });
        d_->pageActions[static_cast<std::size_t>(i)] = action;
    }

    d_->engineActions = new QActionGroup(this);
    for (const Engine engine : {Engine::GStreamer, Engine::Prerendered}) {
        auto* action = new QAction(engineName(engine), this);
        action->setCheckable(true);
        action->setData(engineKey(engine));
        action->setActionGroup(d_->engineActions);
        if (!ReplaySourceFactory::isAvailable(engine)) {
            action->setEnabled(false);
            action->setText(tr("%1 (not in this build)").arg(engineName(engine)));
        }
        connect(action, &QAction::triggered, this, [this, engine] { setEngine(engine, true); });
    }
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(d_->openFolderAction);
    fileMenu->addAction(d_->openBundleAction);
    d_->recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    for (QAction* action : d_->pageActions)
        viewMenu->addAction(action);
    viewMenu->addSeparator();
    viewMenu->addAction(d_->overlayAction);
    viewMenu->addAction(d_->fullScreenAction);

    auto* playbackMenu = menuBar()->addMenu(tr("&Playback"));
    playbackMenu->addAction(d_->playAction);
    playbackMenu->addSeparator();
    playbackMenu->addAction(d_->stepBackAction);
    playbackMenu->addAction(d_->stepForwardAction);
    playbackMenu->addAction(d_->previousSegmentAction);
    playbackMenu->addAction(d_->nextSegmentAction);
    playbackMenu->addSeparator();
    playbackMenu->addAction(d_->renderAction);
    auto* engineMenu = playbackMenu->addMenu(tr("&Engine"));
    engineMenu->addActions(d_->engineActions->actions());

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About abr-viz"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, [this] {
        const auto engines = media::engineSupport();
        QMessageBox box(this);
        box.setWindowTitle(tr("About abr-viz"));
        box.setIconPixmap(appIcon().pixmap(96, 96));
        box.setText(QStringLiteral("<h3>abr-viz %1</h3>").arg(versionString()));
        box.setInformativeText(
            tr("Replays ns-3 adaptive bitrate streaming simulations: the video each simulated viewer "
               "saw, with the stalls, the policy's decisions, the network and the logs.<br><br>"
               "Qt %1 · %2<br>MIT licence.")
                .arg(QString::fromLatin1(qVersion()),
                     engines.gstreamer ? engines.gstreamerVersion : tr("without GStreamer")));
        box.exec();
    });
    auto* aboutQt = helpMenu->addAction(tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBar()
{
    d_->toolBar = addToolBar(tr("Main"));
    d_->toolBar->setObjectName(QStringLiteral("mainToolBar"));
    d_->toolBar->setMovable(false);
    d_->toolBar->setFloatable(false);
    d_->toolBar->setIconSize(QSize(20, 20));
    d_->toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    d_->toolBar->toggleViewAction()->setVisible(false);
    d_->toolBar->addAction(d_->openFolderAction);
    d_->toolBar->addAction(d_->openBundleAction);
    d_->toolBar->addSeparator();

    auto* runLabel = new QLabel(tr("Run "), this);
    runLabel->setForegroundRole(QPalette::PlaceholderText);
    d_->toolBar->addWidget(runLabel);
    d_->runPicker = new QComboBox(this);
    d_->runPicker->setObjectName(QStringLiteral("runPicker"));
    d_->runPicker->setMinimumContentsLength(34);
    d_->runPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    d_->runPicker->setPlaceholderText(tr("No folder open"));
    d_->runPicker->setToolTip(tr("Open another run of the folder"));
    connect(d_->runPicker, &QComboBox::activated, this, [this](int index) {
        const QString directory = d_->runPicker->itemData(index).toString();
        if (!directory.isEmpty())
            openBundle(directory);
    });
    d_->toolBar->addWidget(d_->runPicker);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    d_->toolBar->addWidget(spacer);

    auto* engineLabel = new QLabel(tr("Engine "), this);
    engineLabel->setForegroundRole(QPalette::PlaceholderText);
    d_->toolBar->addWidget(engineLabel);
    d_->engineBox = new QComboBox(this);
    d_->engineBox->setObjectName(QStringLiteral("engineBox"));
    d_->engineBox->setToolTip(tr("GStreamer plays the segment files directly; the pre-rendered "
                                 "engine plays a video rendered once with ffmpeg"));
    for (const Engine engine : ReplaySourceFactory::available())
        d_->engineBox->addItem(engineName(engine), engineKey(engine));
    connect(d_->engineBox, &QComboBox::activated, this, [this](int index) {
        if (const auto engine = engineFromKey(d_->engineBox->itemData(index).toString()))
            setEngine(*engine, true);
    });
    d_->toolBar->addWidget(d_->engineBox);
    d_->toolBar->addAction(d_->renderAction);
}

void MainWindow::applyStyle()
{
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor selectedText = pal.color(QPalette::HighlightedText);
    d_->openFolderAction->setIcon(glyphIcon(Glyph::OpenFolder, text));
    d_->openBundleAction->setIcon(glyphIcon(Glyph::OpenBundle, text));
    d_->stepBackAction->setIcon(glyphIcon(Glyph::StepBack, text));
    d_->stepForwardAction->setIcon(glyphIcon(Glyph::StepForward, text));
    d_->previousSegmentAction->setIcon(glyphIcon(Glyph::PreviousSegment, text));
    d_->nextSegmentAction->setIcon(glyphIcon(Glyph::NextSegment, text));
    d_->renderAction->setIcon(glyphIcon(Glyph::Render, text));
    d_->overlayAction->setIcon(glyphIcon(Glyph::Overlay, text));
    d_->playAction->setIcon(glyphIcon(d_->player->isRunning() ? Glyph::Pause : Glyph::Play, text));
    for (int i = 0; i < kPageCount; ++i) {
        const QIcon icon = glyphIcon(pageGlyph(static_cast<Page>(i)), text, selectedText);
        d_->sidebar->item(i)->setIcon(icon);
        d_->pageActions[static_cast<std::size_t>(i)]->setIcon(glyphIcon(pageGlyph(static_cast<Page>(i)), text));
    }

    // Colours come from the palette, so light and dark mode both work.
    setStyleSheet(QStringLiteral(R"(
        QListWidget#sidebar {
            background: palette(window);
            border: none;
            border-right: 1px solid palette(mid);
            padding-top: 10px;
            font-size: 14px;
        }
        QListWidget#sidebar::item {
            border-radius: 7px;
            margin: 1px 10px;
            padding-left: 6px;
        }
        QListWidget#sidebar::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
        QListWidget#sidebar::item:disabled { color: palette(placeholder-text); }
        QWidget#transportBar { border-top: 1px solid palette(mid); }
        QFrame#card {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 10px;
        }
        QFrame#divider { color: palette(mid); }
        QWidget#momentPanel { background: palette(window); }
    )"));
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // Light and dark mode switches: redraw the icons in the new text colour.
    if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        && d_ && d_->sidebar)
        applyStyle();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(kGeometrySetting, saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreSettings()
{
    const QSettings settings;
    if (settings.contains(kGeometrySetting))
        restoreGeometry(settings.value(kGeometrySetting).toByteArray());
    d_->overlayAction->setChecked(settings.value(kOverlaySetting, false).toBool());
    d_->player->setOverlayVisible(d_->overlayAction->isChecked());
    const QStringList recent = settings.value(kRecentSetting).toStringList();
    d_->library->setRecentFolders(recent);
    d_->recentMenu->clear();
    for (const QString& folder : recent) {
        QAction* action = d_->recentMenu->addAction(QDir::toNativeSeparators(folder));
        connect(action, &QAction::triggered, this, [this, folder] { openFolder(folder); });
    }
    d_->recentMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::rememberFolder(const QString& folder)
{
    QSettings settings;
    QStringList recent = settings.value(kRecentSetting).toStringList();
    recent.removeAll(folder);
    recent.prepend(folder);
    while (recent.size() > kRecentCount)
        recent.removeLast();
    settings.setValue(kRecentSetting, recent);
    restoreSettings();
}

void MainWindow::updateRunPicker()
{
    const QSignalBlocker block(d_->runPicker);
    d_->runPicker->clear();
    LibraryModel* model = d_->library->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const BundleSummaryPtr bundle = model->bundleAt(row);
        const QString link = bundle->linkRate ? format::bitrate(static_cast<double>(*bundle->linkRate))
                                              : tr("uncongested");
        d_->runPicker->addItem(QStringLiteral("%1 · %2").arg(bundle->videoTitle, link), bundle->directory);
    }
    const auto& run = d_->session->run();
    if (run) {
        int index = d_->runPicker->findData(run->info.bundleDirectory);
        if (index < 0) {
            d_->runPicker->addItem(run->info.runId, run->info.bundleDirectory);
            index = d_->runPicker->count() - 1;
        }
        d_->runPicker->setCurrentIndex(index);
    } else {
        d_->runPicker->setCurrentIndex(-1);
    }
    d_->runPicker->setEnabled(d_->runPicker->count() > 0);
}

void MainWindow::stepReplay(double seconds)
{
    if (!d_->session->run())
        return;
    const PlaybackClock& clock = d_->session->clock();
    const double replay = clock.simToReplay(d_->session->cursor()->time()) + seconds;
    d_->session->cursor()->setTime(clock.replayToSim(std::clamp(replay, 0.0, clock.replayDuration())));
}

void MainWindow::stepSegment(int direction)
{
    if (!d_->session->run())
        return;
    const PlaybackClock& clock = d_->session->clock();
    const double now = d_->session->cursor()->time();
    std::optional<double> target;
    for (const PlaybackClock::Piece& piece : clock.pieces()) {
        if (piece.kind != PlaybackClock::Piece::Kind::Segment)
            continue;
        if (direction > 0 && piece.simStart > now + 0.01) {
            target = piece.simStart;
            break;
        }
        // Back: the start of the segment before, unless the cursor is well into this one.
        if (direction < 0 && piece.simStart < now - 0.3)
            target = piece.simStart;
    }
    if (!target && direction < 0)
        target = clock.simStart();
    if (target)
        d_->session->cursor()->setTime(*target);
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
    rememberFolder(d_->scanningFolder);
    updateRunPicker();
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
    if (ok) {
        startLogIndex();
        if (currentPage() == Page::Library)
            showPage(Page::Replay);
    }
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

void MainWindow::updateActions()
{
    const ReplaySource* engine = d_->player->engine();
    const bool run = d_->session->run() != nullptr;
    const bool ready = engine && engine->isReady() && run;
    const bool running = d_->player->isRunning();
    d_->playAction->setEnabled(ready);
    d_->playAction->setText(running ? tr("&Pause") : tr("&Play"));
    d_->playAction->setToolTip(running ? tr("Pause (Space)") : tr("Play (Space)"));
    d_->playAction->setIcon(glyphIcon(running ? Glyph::Pause : Glyph::Play,
                                      palette().color(QPalette::WindowText)));
    for (QAction* action : {d_->stepBackAction, d_->stepForwardAction, d_->previousSegmentAction,
                            d_->nextSegmentAction})
        action->setEnabled(run);
    const bool canRender = engine && run && !engine->isReady()
        && d_->player->engineKind() == Engine::Prerendered
        && engine->state() != ReplaySource::State::Preparing;
    d_->renderAction->setEnabled(canRender);
    d_->renderAction->setVisible(d_->player->engineKind() == Engine::Prerendered);
    d_->transport->setVisible(run);
    for (int i = 1; i < kPageCount; ++i) {
        QListWidgetItem* item = d_->sidebar->item(i);
        item->setFlags(run ? item->flags() | Qt::ItemIsEnabled : item->flags() & ~Qt::ItemIsEnabled);
        d_->pageActions[static_cast<std::size_t>(i)]->setEnabled(run);
    }
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
