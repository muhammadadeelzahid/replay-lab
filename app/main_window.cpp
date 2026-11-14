#include "main_window.h"

#include "branding.h"
#include "charts_panel.h"
#include "format.h"
#include "icons.h"
#include "library_view.h"
#include "log_console.h"
#include "moment_panel.h"
#include "network_path_view.h"
#include "replay_player.h"
#include "report_view.h"
#include "segment_timeline.h"
#include "segmented_control.h"
#include "session.h"
#include "settings_dialog.h"
#include "source_list.h"
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
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSplitter>
#include <QGuiApplication>
#include <QEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <tuple>

namespace abrviz {

namespace {

/** @brief Settings key of the chosen replay engine. */
const QString kEngineSetting = QStringLiteral("replay/engine");
/** @brief Settings key of the recent folders, as absolute paths. */
const QString kRecentSetting = QStringLiteral("library/recentFolders");
/** @brief Settings key of the window geometry. */
const QString kGeometrySetting = QStringLiteral("window/geometry");
/** @brief Settings key of whether the video overlay is shown. */
const QString kOverlaySetting = QStringLiteral("replay/overlay");
/** @brief Settings key of whether the sidebar is shown. */
const QString kSidebarSetting = QStringLiteral("window/sidebar");
/** @brief Settings key of whether the inspector is shown. */
const QString kInspectorSetting = QStringLiteral("window/inspector");
constexpr int kRecentCount = 6;         ///< Most recent folders kept.
constexpr double kStepSeconds = 5.0;    ///< Replay seconds moved by the step actions.

/**
 * @brief Whether a palette is a dark appearance.
 *
 * @param palette The palette to test.
 * @return True if the window colour's lightness is below 128.
 */
bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

/**
 * @brief Wraps content in a page with the usual margins of a macOS window.
 *
 * @param content The widget to wrap; reparented to the page.
 * @param parent The page's parent.
 * @param margin The left and right margin in pixels; the top is 4 pixels less and the
 *               bottom is 6 pixels.
 * @return The new page, owned by `parent`.
 */
QWidget* padded(QWidget* content, QWidget* parent, int margin = 16)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(margin, margin - 4, margin, 6);
    layout->addWidget(content);
    return page;
}

/**
 * @brief A scroll area that scrolls only vertically and is as wide as its content.
 *
 * A tall page scrolls instead of making the window taller than the screen, and the content is
 * never cut off on the right: the area's size hints take the content's width (plus a vertical
 * scroll bar) and follow it when the content's layout changes.
 */
class VerticalScrollArea : public QScrollArea {
public:
    /**
     * @brief Creates the area around content.
     *
     * @param content The widget to scroll; reparented to the area.
     * @param parent The area's parent.
     */
    VerticalScrollArea(QWidget* content, QWidget* parent)
        : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        viewport()->setAutoFillBackground(false);
        setWidget(content);
        content->setAutoFillBackground(false);
        content->installEventFilter(this);
    }

    /// The content's minimum width plus a scroll bar; the height of QScrollArea's hint.
    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return {contentWidth(widget()->minimumSizeHint().expandedTo(widget()->minimumSize()).width()),
                QScrollArea::minimumSizeHint().height()};
    }

    /// The content's preferred size, plus a scroll bar's width.
    [[nodiscard]] QSize sizeHint() const override
    {
        const QSize content = widget()->sizeHint();
        return {contentWidth(content.width()), content.height()};
    }

protected:
    /// Updates the size hints when the content's layout changes.
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == widget() && event->type() == QEvent::LayoutRequest)
            updateGeometry();
        return QScrollArea::eventFilter(watched, event);
    }

private:
    /// A content width plus the vertical scroll bar's.
    [[nodiscard]] int contentWidth(int width) const
    {
        return width + verticalScrollBar()->sizeHint().width();
    }
};

/**
 * @brief Puts content in a VerticalScrollArea.
 *
 * @param content The widget to scroll; reparented to the area.
 * @param parent The area's parent.
 * @return The new area, owned by `parent`.
 */
QScrollArea* scrollable(QWidget* content, QWidget* parent)
{
    return new VerticalScrollArea(content, parent);
}

/**
 * @brief A one-pixel divider line, coloured by the window's style sheet.
 *
 * @param orientation Qt::Vertical for a vertical line, Qt::Horizontal for a horizontal one.
 * @param parent The line's parent.
 * @return The new frame, owned by `parent`.
 */
QFrame* divider(Qt::Orientation orientation, QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("divider"));
    line->setFrameShape(orientation == Qt::Vertical ? QFrame::VLine : QFrame::HLine);
    return line;
}



/**
 * @brief The window's private state: the render queue, the widgets, the actions and the
 *        pending background tasks.
 */
/**
 * @brief The ffmpeg to render with: the one shipped next to the executable in the release
 *        packages if there is one, otherwise the one on the PATH.
 *
 * @return The path of the bundled `ffmpeg` (`ffmpeg.exe` on Windows), or "ffmpeg".
 */
QString bundledFfmpeg()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("ffmpeg.exe");
#else
    const QString name = QStringLiteral("ffmpeg");
#endif
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo(bundled).isExecutable() ? bundled : QStringLiteral("ffmpeg");
}

} // namespace

struct MainWindow::Impl {
    /**
     * @brief Renders the experienced video for the pre-rendered engine.
     *
     * Destroyed with Impl, after shutdown() has destroyed the engine that uses it and before
     * the child widgets.
     */
    std::unique_ptr<RenderQueue> renderQueue;

    // Widgets and watchers are owned by the window through Qt's parent-child tree.
    Session* session = nullptr;           ///< The open run; made a child after the panels.
    LibraryView* library = nullptr;       ///< Library page.
    SummaryPanel* summary = nullptr;      ///< Run summary, beside the report on the Report page.
    ReportView* report = nullptr;         ///< QoE report on the Report page.
    SegmentTimeline* timeline = nullptr;  ///< Segment timeline, shown in the transport bar.
    ChartsPanel* charts = nullptr;        ///< Charts of the Analysis page.
    LogConsole* logs = nullptr;           ///< Log console of the Logs page.
    ReplayPlayer* player = nullptr;       ///< Video replay of the Replay page.
    MomentPanel* moment = nullptr;        ///< Values at the cursor, in the inspector pane.
    TransportBar* transport = nullptr;    ///< Playback controls; shown while a run is open.
    SourceList* sidebar = nullptr;        ///< Library and runs list, in the sidebar pane.
    QWidget* sidebarPane = nullptr;       ///< Sidebar and its divider; see sidebarAction.
    QWidget* inspectorPane = nullptr;     ///< Moment panel and its divider; see updatePanes().
    QStackedWidget* pages = nullptr;      ///< One page per Page value, in the same order.
    SegmentedControl* views = nullptr;    ///< Page switcher; segment i shows Page i + 1.
    QLabel* notice = nullptr;             ///< Message shown by showNotice(); hidden while empty.
    QTimer* noticeTimer = nullptr;        ///< Single-shot timer that hides notice.
    QToolBar* toolBar = nullptr;          ///< The window's only toolbar.
    QMenu* recentMenu = nullptr;          ///< File > Open Recent, rebuilt from the saved folders.
    SettingsDialog* settings = nullptr;   ///< Settings dialog; null until first shown.

    QAction* openFolderAction = nullptr;       ///< Opens a folder of runs.
    QAction* openBundleAction = nullptr;       ///< Opens a single run.
    QAction* playAction = nullptr;             ///< Toggles play and pause.
    QAction* stepBackAction = nullptr;         ///< Moves the replay back by kStepSeconds.
    QAction* stepForwardAction = nullptr;      ///< Moves the replay forward by kStepSeconds.
    QAction* previousSegmentAction = nullptr;  ///< Moves to the previous segment.
    QAction* nextSegmentAction = nullptr;      ///< Moves to the next segment.
    QAction* renderAction = nullptr;           ///< Renders the video for the pre-rendered engine.
    QAction* overlayAction = nullptr;          ///< Checkable; shows the video overlay.
    QAction* fullScreenAction = nullptr;       ///< Toggles full screen.
    QAction* sidebarAction = nullptr;          ///< Checkable; shows the sidebar pane.
    QAction* inspectorAction = nullptr;        ///< Checkable; shows the inspector pane.
    QAction* settingsAction = nullptr;         ///< Opens the settings dialog.
    /** @brief Checkable page actions in an exclusive group, indexed by Page. */
    std::array<QAction*, kPageCount> pageActions{};
    QActionGroup* engineActions = nullptr;  ///< Replay engine choices; data is the engine key.

    QFutureWatcher<QList<BundleSummaryPtr>>* scanWatcher = nullptr;  ///< Watches the folder scan.
    QFutureWatcher<LoadResult>* loadWatcher = nullptr;               ///< Watches the bundle load.
    QFutureWatcher<LogIndexPtr>* logWatcher = nullptr;               ///< Watches the log index.

    // Pending tasks. The guards cancel and wait when replaced or destroyed.
    FutureGuard<QList<BundleSummaryPtr>> scan;   ///< Folder scan; empty if none is pending.
    FutureGuard<LoadResult> load;                ///< Bundle load; empty if none is pending.
    FutureGuard<LogIndexPtr> logIndex;           ///< Log index build; empty if none is pending.
    std::shared_ptr<const RunData> logIndexRun;  ///< The run the pending index is for.
    QString scanningFolder;  ///< Absolute path of the folder last scanned.
    QString loadingBundle;   ///< Absolute path of the bundle last loaded.
    /** @brief The page to show once the bundle being loaded opens (More Info: the report). */
    std::optional<Page> pageAfterOpen;
    bool shutDown = false;   ///< Set by shutdown(); no new tasks start after it.
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , d_(std::make_unique<Impl>())
{
    setWindowTitle(appDisplayName());
    setWindowIcon(appIcon());
    // Qt's unified title and toolbar draws the window black around custom widgets; the
    // toolbar sits under the title bar instead, as in Preview or TextEdit.
    setUnifiedTitleAndToolBarOnMac(false);
    // 1440 x 900, or less on a smaller screen: the window fits the screen's usable area
    // (without the menu bar and the Dock).
    QSize size(1440, 900);
    if (const QScreen* screen = QGuiApplication::primaryScreen())
        size = size.boundedTo(screen->availableGeometry().size() * 0.92);
    resize(size);

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
    renderOptions.ffmpeg = environment.value(QStringLiteral("ABRVIZ_FFMPEG"), bundledFfmpeg());
    d_->renderQueue = std::make_unique<RenderQueue>(renderOptions);

    d_->player = new ReplayPlayer(d_->session, d_->renderQueue.get(), this);
    createActions();
    createPages();
    createMenus();
    createToolBar();
    session.release()->setParent(this);

    d_->scanWatcher = new QFutureWatcher<QList<BundleSummaryPtr>>(this);
    connect(d_->scanWatcher, &QFutureWatcherBase::finished, this, &MainWindow::scanFinished);
    d_->loadWatcher = new QFutureWatcher<LoadResult>(this);
    connect(d_->loadWatcher, &QFutureWatcherBase::finished, this, &MainWindow::loadFinished);
    d_->logWatcher = new QFutureWatcher<LogIndexPtr>(this);
    connect(d_->logWatcher, &QFutureWatcherBase::finished, this, &MainWindow::logIndexFinished);

    connect(d_->library, &LibraryView::openRequested, this, &MainWindow::openBundle);
    connect(d_->library, &LibraryView::infoRequested, this, [this](const QString& directory) {
        d_->pageAfterOpen = Page::Report;
        openBundle(directory);
    });
    // Stills of the runs' videos for the library's cards, kept beside the rendered videos when
    // ABRVIZ_RENDER_CACHE says where those go.
    d_->library->setThumbnailSource(
        renderOptions.ffmpeg,
        renderOptions.cacheDirectory.isEmpty() ? QString()
                                               : QDir(renderOptions.cacheDirectory).filePath(QStringLiteral("thumbnails")));
    connect(d_->library, &LibraryView::openFolderRequested, this, &MainWindow::chooseFolder);
    connect(d_->library, &LibraryView::openBundleRequested, this, &MainWindow::chooseBundle);
    connect(d_->library, &LibraryView::recentFolderRequested, this, &MainWindow::openFolder);
    connect(d_->sidebar, &SourceList::allRunsSelected, this, [this] { showPage(Page::Library); });
    connect(d_->sidebar, &SourceList::runSelected, this, &MainWindow::openBundle);
    connect(d_->session, &Session::runChanged, this, [this] {
        updateTitle();
        updateActions();
        updatePanes();
        if (const auto& run = d_->session->run())
            d_->sidebar->selectRun(run->info.bundleDirectory);
    });
    connect(d_->player, &ReplayPlayer::stateChanged, this, &MainWindow::updateActions);
    connect(d_->player, &ReplayPlayer::engineChanged, this, &MainWindow::updateActions);
    connect(d_->player, &ReplayPlayer::engineChanged, this, &MainWindow::updateEngineActions);
    connect(d_->player, &ReplayPlayer::engineFellBack, this, [this](const QString& reason) {
        showNotice(tr("GStreamer could not play this run (%1). Using the pre-rendered engine.").arg(reason), 15000);
    });
    d_->player->setEngine(savedEngine());
    restoreSettings();
    applyStyle();
    showPage(Page::Library);
    updateActions();
    updatePanes();
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
SourceList* MainWindow::sidebar() const { return d_->sidebar; }

bool MainWindow::isBusy() const
{
    return d_->scan.isRunning() || d_->load.isRunning() || d_->logIndex.isRunning();
}

QString MainWindow::noticeText() const
{
    return d_->notice->isVisibleTo(this) ? d_->notice->text() : QString();
}

void MainWindow::showNotice(const QString& text, int milliseconds)
{
    d_->notice->setText(text);
    d_->notice->setVisible(!text.isEmpty());
    if (milliseconds > 0)
        d_->noticeTimer->start(milliseconds);
    else
        d_->noticeTimer->stop();
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
    if (d_->settings && current)
        d_->settings->setEngine(*current);
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
    d_->pageActions[static_cast<std::size_t>(index)]->setChecked(true);
    // The segmented control shows the run's views; the library is chosen in the sidebar.
    d_->views->setCurrentIndex(page == Page::Library ? -1 : index - 1);
    if (page == Page::Library)
        d_->sidebar->selectAllRuns();
    else if (const auto& run = d_->session->run())
        d_->sidebar->selectRun(run->info.bundleDirectory);
    updatePanes();
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
    // The welcome screen, the library and the charts scroll when the window is short, so the
    // window's minimum height stays well under a laptop screen's (with the Dock showing).
    d_->pages->addWidget(scrollable(d_->library, this));
    // The replay, under the path its frames took: server, satellite, IAB relay, viewer.
    auto* replay = new QWidget(this);
    auto* replayLayout = new QVBoxLayout(replay);
    replayLayout->setContentsMargins(0, 0, 0, 0);
    replayLayout->setSpacing(0);
    replayLayout->addWidget(new NetworkPathView(d_->session, d_->player, replay));
    replayLayout->addWidget(divider(Qt::Horizontal, replay));
    replayLayout->addWidget(d_->player, 1);
    d_->pages->addWidget(replay);
    d_->pages->addWidget(scrollable(padded(d_->charts, this), this));
    d_->pages->addWidget(padded(d_->logs, this));
    auto* reportSplit = new QSplitter(Qt::Horizontal, this);
    reportSplit->addWidget(d_->report);
    reportSplit->addWidget(d_->summary);
    reportSplit->setStretchFactor(0, 3);
    reportSplit->setStretchFactor(1, 2);
    reportSplit->setChildrenCollapsible(false);
    reportSplit->setHandleWidth(1);
    d_->pages->addWidget(padded(reportSplit, this));

    d_->sidebar = new SourceList(this);
    d_->sidebarPane = new QWidget(this);
    auto* sidebarColumn = new QWidget(d_->sidebarPane);
    sidebarColumn->setObjectName(QStringLiteral("sidebarColumn"));
    sidebarColumn->setFixedWidth(230);
    auto* columnLayout = new QVBoxLayout(sidebarColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    columnLayout->addWidget(d_->sidebar, 1);
    // Says, under every list of runs, what network they were streamed over.
    auto* footer = new QWidget(sidebarColumn);
    footer->setObjectName(QStringLiteral("sidebarFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 12, 12);
    footerLayout->setSpacing(10);
    auto* footerIcon = new QLabel(footer);
    footerIcon->setObjectName(QStringLiteral("networkIcon"));
    footerIcon->setAlignment(Qt::AlignTop);
    auto* footerText = new QLabel(tr("<b>Satellite 5G with IAB</b><br>Every run was streamed from a "
                                     "LEO satellite gNB through an IAB relay to the viewer."),
                                  footer);
    footerText->setWordWrap(true);
    footerText->setFont(format::scaled(footerText->font(), 0.9));
    footerText->setForegroundRole(QPalette::PlaceholderText);
    footerLayout->addWidget(footerIcon);
    footerLayout->addWidget(footerText, 1);
    columnLayout->addWidget(footer);
    auto* sidebarLayout = new QHBoxLayout(d_->sidebarPane);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebarColumn);
    sidebarLayout->addWidget(divider(Qt::Vertical, d_->sidebarPane));

    d_->inspectorPane = new QWidget(this);
    auto* inspectorLayout = new QHBoxLayout(d_->inspectorPane);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    inspectorLayout->addWidget(divider(Qt::Vertical, d_->inspectorPane));
    inspectorLayout->addWidget(scrollable(d_->moment, d_->inspectorPane));

    d_->transport = new TransportBar(d_->session, d_->player, d_->timeline,
                                     {d_->previousSegmentAction, d_->stepBackAction, d_->playAction,
                                      d_->stepForwardAction, d_->nextSegmentAction},
                                     this);

    d_->notice = new QLabel(this);
    d_->notice->setObjectName(QStringLiteral("notice"));
    d_->notice->setWordWrap(true);
    d_->notice->setAlignment(Qt::AlignCenter);
    d_->notice->hide();
    d_->noticeTimer = new QTimer(this);
    d_->noticeTimer->setSingleShot(true);
    connect(d_->noticeTimer, &QTimer::timeout, d_->notice, &QWidget::hide);

    auto* middle = new QWidget(this);
    auto* middleLayout = new QHBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addWidget(d_->pages, 1);
    middleLayout->addWidget(d_->inspectorPane);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    auto* noticeRow = new QHBoxLayout();
    noticeRow->setContentsMargins(16, 8, 16, 0);
    noticeRow->addWidget(d_->notice);
    contentLayout->addLayout(noticeRow);
    contentLayout->addWidget(middle, 1);
    contentLayout->addWidget(divider(Qt::Horizontal, content));
    contentLayout->addWidget(d_->transport);

    auto* central = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(d_->sidebarPane);
    centralLayout->addWidget(content, 1);
    setCentralWidget(central);
}

void MainWindow::createActions()
{
    auto make = [this](const QString& text, const QKeySequence& shortcut = {}) {
        auto* action = new QAction(text, this);
        action->setShortcut(shortcut);
        addAction(action);  // shortcuts work whichever view is shown
        return action;
    };
    d_->openFolderAction = make(tr("Open Folder..."), QKeySequence::Open);
    d_->openFolderAction->setToolTip(tr("Open a folder of runs"));
    connect(d_->openFolderAction, &QAction::triggered, this, &MainWindow::chooseFolder);
    d_->openBundleAction = make(tr("Open Run..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    d_->openBundleAction->setToolTip(tr("Open a single run"));
    connect(d_->openBundleAction, &QAction::triggered, this, &MainWindow::chooseBundle);

    d_->playAction = make(tr("Play"), Qt::Key_Space);
    connect(d_->playAction, &QAction::triggered, d_->player, &ReplayPlayer::togglePlay);
    d_->stepBackAction = make(tr("Back 5 Seconds"), QKeySequence(Qt::ALT | Qt::Key_Left));
    d_->stepBackAction->setToolTip(tr("Back 5 seconds"));
    connect(d_->stepBackAction, &QAction::triggered, this, [this] { stepReplay(-kStepSeconds); });
    d_->stepForwardAction = make(tr("Forward 5 Seconds"), QKeySequence(Qt::ALT | Qt::Key_Right));
    d_->stepForwardAction->setToolTip(tr("Forward 5 seconds"));
    connect(d_->stepForwardAction, &QAction::triggered, this, [this] { stepReplay(kStepSeconds); });
    d_->previousSegmentAction = make(tr("Previous Segment"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
    connect(d_->previousSegmentAction, &QAction::triggered, this, [this] { stepSegment(-1); });
    d_->nextSegmentAction = make(tr("Next Segment"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
    connect(d_->nextSegmentAction, &QAction::triggered, this, [this] { stepSegment(1); });
    d_->renderAction = make(tr("Render Video"), QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(d_->renderAction, &QAction::triggered, d_->player, &ReplayPlayer::prepare);

    d_->overlayAction = make(tr("Show Video Overlay"), QKeySequence(Qt::CTRL | Qt::Key_I));
    d_->overlayAction->setCheckable(true);
    connect(d_->overlayAction, &QAction::toggled, this, [this](bool on) {
        d_->player->setOverlayVisible(on);
        if (d_->settings)
            d_->settings->setOverlayVisible(on);
        QSettings settings;
        settings.setValue(kOverlaySetting, on);
    });
    d_->sidebarAction = make(tr("Show Sidebar"), QKeySequence(Qt::CTRL | Qt::META | Qt::Key_S));
    d_->sidebarAction->setCheckable(true);
    d_->sidebarAction->setChecked(true);
    d_->sidebarAction->setToolTip(tr("Show or hide the sidebar"));
    connect(d_->sidebarAction, &QAction::toggled, this, [this](bool on) {
        d_->sidebarPane->setVisible(on);
        QSettings settings;
        settings.setValue(kSidebarSetting, on);
    });
    d_->inspectorAction = make(tr("Show Inspector"), QKeySequence(Qt::ALT | Qt::CTRL | Qt::Key_I));
    d_->inspectorAction->setCheckable(true);
    d_->inspectorAction->setChecked(true);
    d_->inspectorAction->setToolTip(tr("Show or hide the inspector"));
    connect(d_->inspectorAction, &QAction::toggled, this, [this](bool on) {
        QSettings settings;
        settings.setValue(kInspectorSetting, on);
        updatePanes();
    });
    d_->fullScreenAction = make(tr("Enter Full Screen"), QKeySequence::FullScreen);
    connect(d_->fullScreenAction, &QAction::triggered, this, [this] {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    });
    d_->settingsAction = make(tr("Settings..."), QKeySequence::Preferences);
    d_->settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(d_->settingsAction, &QAction::triggered, this, &MainWindow::showSettings);

    auto* pageGroup = new QActionGroup(this);
    for (int i = 0; i < kPageCount; ++i) {
        const auto page = static_cast<Page>(i);
        const QString text = page == Page::Library ? tr("All Runs") : pageName(page);
        QAction* action = make(text, QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
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
    auto* fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(d_->openFolderAction);
    fileMenu->addAction(d_->openBundleAction);
    d_->recentMenu = fileMenu->addMenu(tr("Open Recent"));
    fileMenu->addSeparator();
    auto* closeAction = fileMenu->addAction(tr("Close Window"));
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
    // Moved to the application menu on macOS.
    fileMenu->addAction(d_->settingsAction);
    auto* quitAction = fileMenu->addAction(tr("Quit %1").arg(appDisplayName()));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("View"));
    viewMenu->addAction(d_->sidebarAction);
    viewMenu->addAction(d_->inspectorAction);
    viewMenu->addSeparator();
    for (QAction* action : d_->pageActions)
        viewMenu->addAction(action);
    viewMenu->addSeparator();
    viewMenu->addAction(d_->overlayAction);
    viewMenu->addSeparator();
    viewMenu->addAction(d_->fullScreenAction);

    auto* playbackMenu = menuBar()->addMenu(tr("Playback"));
    playbackMenu->addAction(d_->playAction);
    playbackMenu->addSeparator();
    playbackMenu->addAction(d_->stepBackAction);
    playbackMenu->addAction(d_->stepForwardAction);
    playbackMenu->addAction(d_->previousSegmentAction);
    playbackMenu->addAction(d_->nextSegmentAction);
    playbackMenu->addSeparator();
    playbackMenu->addAction(d_->renderAction);
    auto* engineMenu = playbackMenu->addMenu(tr("Engine"));
    engineMenu->addActions(d_->engineActions->actions());

    auto* windowMenu = menuBar()->addMenu(tr("Window"));
    auto* minimize = windowMenu->addAction(tr("Minimize"));
    minimize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(minimize, &QAction::triggered, this, &QWidget::showMinimized);
    auto* zoom = windowMenu->addAction(tr("Zoom"));
    connect(zoom, &QAction::triggered, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });

    auto* helpMenu = menuBar()->addMenu(tr("Help"));
    auto* aboutAction = helpMenu->addAction(tr("About %1").arg(appDisplayName()));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, [this] {
        const auto engines = media::engineSupport();
        QMessageBox box(this);
        box.setWindowTitle(tr("About %1").arg(appDisplayName()));
        box.setIconPixmap(appIcon().pixmap(96, 96));
        box.setText(QStringLiteral("<b>%1</b>").arg(appDisplayName()));
        box.setInformativeText(
            tr("Version %1<br><br>Replays ns-3 adaptive bitrate streaming simulations: the video each "
               "simulated viewer saw, with the stalls, the policy's decisions, the network and the "
               "logs.<br><br>Qt %2 · %3<br>MIT licence.")
                .arg(versionString(), QString::fromLatin1(qVersion()),
                     engines.gstreamer ? engines.gstreamerVersion : tr("without GStreamer")));
        box.exec();
    });
    auto* helpAction = helpMenu->addAction(tr("%1 Help").arg(appDisplayName()));
    connect(helpAction, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/muhammadadeelzahid/abr-viz#readme")));
    });
}

void MainWindow::createToolBar()
{
    d_->toolBar = addToolBar(tr("Toolbar"));
    d_->toolBar->setObjectName(QStringLiteral("mainToolBar"));
    d_->toolBar->setMovable(false);
    d_->toolBar->setFloatable(false);
    d_->toolBar->setIconSize(QSize(18, 18));
    d_->toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    d_->toolBar->toggleViewAction()->setVisible(false);
    // Toolbar toggles are plain buttons (as in Finder and Mail) that flip the View menu's items.
    auto* sidebarButton = new QAction(d_->sidebarAction->icon(), d_->sidebarAction->toolTip(), this);
    sidebarButton->setObjectName(QStringLiteral("sidebarButton"));
    connect(sidebarButton, &QAction::triggered, d_->sidebarAction, &QAction::toggle);
    d_->toolBar->addAction(sidebarButton);
    d_->toolBar->addAction(d_->openFolderAction);

    auto spacer = [this] {
        auto* widget = new QWidget(this);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return widget;
    };
    d_->toolBar->addWidget(spacer());
    d_->views = new SegmentedControl(this);
    for (int i = 1; i < kPageCount; ++i) {
        const auto page = static_cast<Page>(i);
        d_->views->addSegment(pageName(page),
                              d_->pageActions[static_cast<std::size_t>(i)]->shortcut().toString(QKeySequence::NativeText));
    }
    connect(d_->views, &SegmentedControl::currentChanged, this,
            [this](int index) { showPage(static_cast<Page>(index + 1)); });
    d_->toolBar->addWidget(d_->views);
    d_->toolBar->addWidget(spacer());
    auto* inspectorButton = new QAction(d_->inspectorAction->icon(), d_->inspectorAction->toolTip(), this);
    inspectorButton->setObjectName(QStringLiteral("inspectorButton"));
    connect(inspectorButton, &QAction::triggered, d_->inspectorAction, &QAction::toggle);
    d_->toolBar->addAction(inspectorButton);
}

void MainWindow::showSettings()
{
    if (!d_->settings) {
        d_->settings = new SettingsDialog(d_->renderQueue->cache().directory(), this);
        connect(d_->settings, &SettingsDialog::engineChosen, this,
                [this](Engine engine) { setEngine(engine, true); });
        connect(d_->settings, &SettingsDialog::overlayToggled, d_->overlayAction, &QAction::setChecked);
    }
    if (const auto engine = d_->player->engineKind())
        d_->settings->setEngine(*engine);
    d_->settings->setOverlayVisible(d_->overlayAction->isChecked());
    d_->settings->show();
    d_->settings->raise();
    d_->settings->activateWindow();
}

void MainWindow::updateTitle()
{
    const auto& run = d_->session->run();
    if (!run) {
        setWindowTitle(appDisplayName());
        return;
    }
    const QString link = run->info.feederLinkRate
        ? format::bitrate(static_cast<double>(*run->info.feederLinkRate))
        : tr("uncongested");
    const QString network = run->topology.hasSatellite() ? tr(" · Satellite 5G IAB") : QString();
    setWindowTitle(QStringLiteral("%1 — %2 feeder%3").arg(run->video.title, link, network));
}

void MainWindow::updatePanes()
{
    const bool run = d_->session->run() != nullptr;
    d_->inspectorPane->setVisible(run && d_->inspectorAction->isChecked() && currentPage() != Page::Library);
    d_->transport->setVisible(run);
}

void MainWindow::applyStyle()
{
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    d_->openFolderAction->setIcon(glyphIcon(Glyph::OpenFolder, text));
    d_->openBundleAction->setIcon(glyphIcon(Glyph::OpenBundle, text));
    d_->stepBackAction->setIcon(glyphIcon(Glyph::StepBack, text));
    d_->stepForwardAction->setIcon(glyphIcon(Glyph::StepForward, text));
    d_->previousSegmentAction->setIcon(glyphIcon(Glyph::PreviousSegment, text));
    d_->nextSegmentAction->setIcon(glyphIcon(Glyph::NextSegment, text));
    for (QAction* action : findChildren<QAction*>()) {
        if (action->objectName() == QLatin1String("sidebarButton"))
            action->setIcon(glyphIcon(Glyph::Sidebar, text));
        else if (action->objectName() == QLatin1String("inspectorButton"))
            action->setIcon(glyphIcon(Glyph::Inspector, text));
    }
    d_->playAction->setIcon(glyphIcon(d_->player->isRunning() ? Glyph::Pause : Glyph::Play, text));
    if (auto* icon = findChild<QLabel*>(QStringLiteral("networkIcon")))
        icon->setPixmap(glyphIcon(Glyph::Satellite, accentColor(pal)).pixmap(22, 22));

    const bool dark = isDark(pal);
    const QString line = dark ? QStringLiteral("#1affffff") : QStringLiteral("#1f000000");
    const QString grouped = dark ? QStringLiteral("#0dffffff") : QStringLiteral("#09000000");
    const QString noticeBg = dark ? QStringLiteral("#29ffd60a") : QStringLiteral("#38ffcc00");
    setStyleSheet(QStringLiteral(R"(
        QFrame#divider { color: %1; background: %1; border: none; max-width: 1px; max-height: 1px; }
        QFrame#divider[frameShape="5"] { max-height: 16777215px; }
        QFrame#divider[frameShape="4"] { max-width: 16777215px; }
        QFrame#card { background: %2; border: none; border-radius: 10px; }
        QLabel#notice { background: %3; border-radius: 8px; padding: 6px 12px; }
        QWidget#sidebarColumn, QWidget#sidebarFooter { background: %4; }
        QSplitter::handle { background: %1; }
    )").arg(line, grouped, noticeBg, dark ? QStringLiteral("#262628") : QStringLiteral("#ececee")));
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // Light and dark mode switches: redraw the icons in the new text colour.
    // Setting a style sheet changes the palette too, so the restyle must not re-enter itself.
    static bool restyling = false;
    if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        && d_ && d_->sidebar && !restyling) {
        restyling = true;
        applyStyle();
        restyling = false;
    }
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
    if (settings.contains(kGeometrySetting)) {
        restoreGeometry(settings.value(kGeometrySetting).toByteArray());
        // A window saved on a bigger screen, or taller than the old minimum size allowed, is
        // moved and shrunk into the usable area of the screen it is on.
        if (!isMaximized() && !isFullScreen()) {
            const QRect available = screen()->availableGeometry();
            QRect frame = frameGeometry();
            const QSize extra = frame.size() - geometry().size();  // title bar and borders
            const QSize size = geometry().size().boundedTo(available.size() - extra);
            frame.setSize(size + extra);
            frame.moveTop(std::clamp(frame.top(), available.top(), available.bottom() - frame.height() + 1));
            frame.moveLeft(std::clamp(frame.left(), available.left(), available.right() - frame.width() + 1));
            resize(size);
            move(frame.topLeft());
        }
    }
    d_->overlayAction->setChecked(settings.value(kOverlaySetting, false).toBool());
    d_->player->setOverlayVisible(d_->overlayAction->isChecked());
    d_->sidebarAction->setChecked(settings.value(kSidebarSetting, true).toBool());
    d_->inspectorAction->setChecked(settings.value(kInspectorSetting, true).toBool());
    const QStringList recent = settings.value(kRecentSetting).toStringList();
    d_->library->setRecentFolders(recent);
    d_->recentMenu->clear();
    for (const QString& folder : recent) {
        QAction* action = d_->recentMenu->addAction(QDir(folder).dirName());
        action->setToolTip(QDir::toNativeSeparators(folder));
        connect(action, &QAction::triggered, this, [this, folder] { openFolder(folder); });
    }
    if (!recent.isEmpty()) {
        d_->recentMenu->addSeparator();
        QAction* clear = d_->recentMenu->addAction(tr("Clear Menu"));
        connect(clear, &QAction::triggered, this, [this] {
            QSettings s;
            s.remove(kRecentSetting);
            restoreSettings();
        });
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

void MainWindow::updateSidebar()
{
    LibraryModel* model = d_->library->model();
    QList<BundleSummaryPtr> runs;
    for (int row = 0; row < model->rowCount(); ++row)
        runs.append(model->bundleAt(row));
    d_->sidebar->setRuns(runs);
    if (currentPage() == Page::Library)
        d_->sidebar->selectAllRuns();
    else if (const auto& run = d_->session->run())
        d_->sidebar->selectRun(run->info.bundleDirectory);
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
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open a Folder of Runs"));
    if (!folder.isEmpty())
        openFolder(folder);
}

void MainWindow::chooseBundle()
{
    const QString bundle = QFileDialog::getExistingDirectory(this, tr("Open a Run"));
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
    updateSidebar();
    updateStatus();
    emit folderScanned(d_->scanningFolder);
}

void MainWindow::loadFinished()
{
    const auto future = d_->loadWatcher->future();
    std::ignore = d_->load.release();
    const std::optional<Page> pageAfterOpen = std::exchange(d_->pageAfterOpen, std::nullopt);
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
        if (pageAfterOpen)
            showPage(*pageAfterOpen);
        else if (currentPage() == Page::Library)
            showPage(Page::Replay);
    }
    updateStatus();
    if (!ok)
        showNotice(tr("Could not open %1.").arg(QDir(d_->loadingBundle).dirName()), 10000);
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
    d_->playAction->setText(running ? tr("Pause") : tr("Play"));
    d_->playAction->setToolTip(running ? tr("Pause (Space)") : tr("Play (Space)"));
    d_->playAction->setIcon(glyphIcon(running ? Glyph::Pause : Glyph::Play,
                                      palette().color(QPalette::WindowText)));
    for (QAction* action : {d_->stepBackAction, d_->stepForwardAction, d_->previousSegmentAction,
                            d_->nextSegmentAction})
        action->setEnabled(run);
    d_->renderAction->setEnabled(engine && run && !engine->isReady()
                                 && d_->player->engineKind() == Engine::Prerendered
                                 && engine->state() != ReplaySource::State::Preparing);
    for (int i = 1; i < kPageCount; ++i) {
        d_->pageActions[static_cast<std::size_t>(i)]->setEnabled(run);
        d_->views->setSegmentEnabled(i - 1, run);
    }
}

void MainWindow::updateStatus()
{
    if (d_->load.isRunning())
        showNotice(tr("Opening %1 ...").arg(QDir(d_->loadingBundle).dirName()), 0);
    else if (d_->scan.isRunning())
        showNotice(tr("Reading %1 ...").arg(QDir(d_->scanningFolder).dirName()), 0);
    else if (d_->noticeTimer && !d_->noticeTimer->isActive())
        showNotice(QString());
}

} // namespace abrviz
