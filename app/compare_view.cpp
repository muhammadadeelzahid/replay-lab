#include "compare_view.h"

#include "replay_player.h"
#include "run_pane.h"
#include "segmented_control.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/load/bundle_loader.h"
#include "abrviz/media/replay_source.h"

#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace abrviz {

namespace {

constexpr int kSyncMs = 100;          ///< How often the other panes are checked.
constexpr double kAheadS = 0.05;      ///< A pane further ahead than this waits.
constexpr double kCaughtUpS = 0.02;   ///< A waiting pane resumes within this of its target.
constexpr double kBehindS = 0.25;     ///< A playing pane further behind than this is moved forward.
constexpr double kStalledBehindS = 0.05;  ///< A pane frozen in a stall is moved this early.
constexpr double kPausedSlackS = 0.02;  ///< While paused, panes are moved if further off.

/// The number of panes a layout shows.
int panesFor(CompareView::Layout layout, int available)
{
    switch (layout) {
    case CompareView::Layout::Single: return 1;
    case CompareView::Layout::SideBySide: return std::min(2, available);
    case CompareView::Layout::Grid: return std::min(4, available);
    }
    return 1;
}

/// The absolute path of a directory, for comparing.
QString absolute(const QString& directory)
{
    return QDir(directory).absolutePath();
}

} // namespace

CompareView::CompareView(Session* session, ReplayPlayer* player, RenderQueue* queue, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
    , queue_(queue)
    , main_(new RunPane(session, player, this))
    , bar_(new QWidget(this))
    , layoutSwitch_(new SegmentedControl(bar_))
    , alignmentSwitch_(new SegmentedControl(bar_))
    , grid_(new QGridLayout())
    , syncTimer_(new QTimer(this))
{
    setObjectName(QStringLiteral("compareView"));
    layoutSwitch_->addSegment(tr("Single"), tr("The open run alone"));
    layoutSwitch_->addSegment(tr("Side by side"), tr("The open run and one other algorithm"));
    layoutSwitch_->addSegment(tr("Grid"), tr("Up to four algorithms"));
    layoutSwitch_->setCurrentIndex(0);
    alignmentSwitch_->addSegment(tr("Same moment"), tr("Each pane shows what its user saw at the same time "
                                                     "since the stream started; stalls show as freezes"));
    alignmentSwitch_->addSegment(tr("Same frame"), tr("Each pane shows the same frame of the video, at the "
                                                    "quality its algorithm played it; stalls are skipped"));
    alignmentSwitch_->setCurrentIndex(0);
    connect(layoutSwitch_, &SegmentedControl::currentChanged, this,
            [this](int index) { setLayoutMode(static_cast<Layout>(index)); });
    connect(alignmentSwitch_, &SegmentedControl::currentChanged, this, [this](int index) {
        setAlignment(index == 0 ? ComparisonClock::Alignment::SessionTime : ComparisonClock::Alignment::MediaTime);
    });

    auto* label = new QLabel(tr("Compare"), bar_);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    auto* barLayout = new QHBoxLayout(bar_);
    barLayout->setContentsMargins(12, 4, 12, 4);
    barLayout->setSpacing(10);
    barLayout->addWidget(label);
    barLayout->addWidget(layoutSwitch_);
    barLayout->addStretch(1);
    barLayout->addWidget(alignmentSwitch_);
    bar_->hide();

    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(2);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar_);
    layout->addLayout(grid_, 1);
    placePanes();

    syncTimer_->setInterval(kSyncMs);
    connect(syncTimer_, &QTimer::timeout, this, &CompareView::sync);
    connect(session_, &Session::runChanged, this, &CompareView::mainRunChanged);
    // A jump of the cursor (a seek, a click on the timeline) moves the others at once.
    connect(session_->cursor(), &TimeCursor::timeChanged, this, [this](double, TimeCursor::Source source) {
        if (!steering_ && source != TimeCursor::Source::Player)
            sync();
    });
    connect(player_, &ReplayPlayer::stateChanged, this, [this] {
        // The user paused while the first pane was waiting: stop waiting.
        if (!steering_ && mainHeld_ && !player_->isRunning())
            mainHeld_ = false;
        sync();
    });
    mainRunChanged();
}

CompareView::~CompareView()
{
    syncTimer_->stop();
    // Each pane (and its player) goes before the session it shows; the loads are cancelled
    // and waited for by their guards.
    for (auto& follower : followers_) {
        if (follower->watcher)
            follower->watcher->disconnect(this);
        delete follower->pane;
        delete follower->session;
    }
    followers_.clear();
}

void CompareView::setLibrary(const QList<BundleSummaryPtr>& bundles)
{
    library_ = bundles;
    mainRunChanged();
}

void CompareView::setLayoutMode(Layout layout)
{
    preferred_ = layout;
    if (peers_.size() < 2)
        layout = Layout::Single;
    if (layout == layout_ && paneCount() == panesFor(layout, static_cast<int>(peers_.size())))
        return;
    layout_ = layout;
    rebuildPanes();
}

void CompareView::setAlignment(ComparisonClock::Alignment alignment)
{
    if (alignment == alignment_)
        return;
    alignment_ = alignment;
    {
        const QSignalBlocker block(alignmentSwitch_);
        alignmentSwitch_->setCurrentIndex(alignment == ComparisonClock::Alignment::SessionTime ? 0 : 1);
    }
    rebuildClock();
    sync();
}

int CompareView::paneCount() const
{
    return 1 + static_cast<int>(followers_.size());
}

RunPane* CompareView::pane(int index) const
{
    if (index == 0)
        return main_;
    if (index < 1 || index >= paneCount())
        return nullptr;
    return followers_[static_cast<std::size_t>(index - 1)]->pane;
}

double CompareView::drift(int index) const
{
    if (index < 1 || index >= paneCount() || index >= clock_.runCount() || !clock_.run(index))
        return 0.0;
    const ReplaySource* mainEngine = player_->engine();
    const ReplaySource* engine = pane(index)->player()->engine();
    if (!mainEngine || !engine)
        return 0.0;
    return engine->position() - clock_.positionOf(index, masterTime()).replay;
}

double CompareView::frameDrift(int index) const
{
    if (index < 1 || index >= paneCount() || index >= clock_.runCount() || !clock_.run(index))
        return 0.0;
    const ReplaySource* mainEngine = player_->engine();
    const ReplaySource* engine = pane(index)->player()->engine();
    if (!mainEngine || !engine)
        return 0.0;
    const QVariant mainFrame = mainEngine->property("frameMediaTime");
    const QVariant frame = engine->property("frameMediaTime");
    if (!mainFrame.isValid() || !frame.isValid())
        return drift(index);
    // The master time of the first pane's frame, and the frame the other pane should show.
    const double master = clock_.masterOf(0, clock_.clock(0).mediaToReplay(mainFrame.toDouble()));
    const double expected = clock_.clock(index).replayToMedia(clock_.positionOf(index, master).replay);
    return frame.toDouble() - expected;
}

bool CompareView::bothPlaying(int index) const
{
    if (index < 1 || index >= paneCount() || index >= clock_.runCount() || !clock_.run(index))
        return false;
    const double master = masterTime();
    const auto playing = [&](int i) {
        const ComparisonClock::RunPosition p = clock_.positionOf(i, master);
        return !p.starting && !p.ended && !p.position.stalled;
    };
    return player_->isRunning() && playing(0) && playing(index);
}

bool CompareView::isReady() const
{
    for (const auto& follower : followers_) {
        if (!follower->session->run() || follower->load.isRunning())
            return false;
        const ReplaySource* engine = follower->pane->player()->engine();
        if (engine && (!engine->isReady() || engine->state() == ReplaySource::State::Preparing))
            return false;
    }
    return true;
}

void CompareView::mainRunChanged()
{
    // The open run's experiment: same video, rate and run number.
    peers_.clear();
    const auto& run = session_->run();
    if (run) {
        const QString directory = absolute(run->info.bundleDirectory);
        for (const Experiment& experiment : groupExperiments(library_)) {
            for (const BundleSummaryPtr& member : experiment.runs) {
                if (absolute(member->directory) == directory) {
                    peers_ = experiment.runs;
                    break;
                }
            }
            if (!peers_.isEmpty())
                break;
        }
    }
    if (peers_.size() < 2)
        peers_.clear();
    layout_ = peers_.size() >= 2 ? preferred_ : Layout::Single;
    rebuildPanes();
}

QStringList CompareView::defaultFollowers(int count) const
{
    // The other algorithms of the experiment in the usual order (LLM first), skipping the
    // open run.
    QStringList directories;
    const QString open = session_->run() ? absolute(session_->run()->info.bundleDirectory) : QString();
    for (const BundleSummaryPtr& member : peers_) {
        if (directories.size() >= count)
            break;
        if (absolute(member->directory) != open)
            directories << member->directory;
    }
    return directories;
}

void CompareView::rebuildPanes()
{
    const int wanted = panesFor(layout_, static_cast<int>(peers_.size())) - 1;
    const QStringList choices = defaultFollowers(wanted);

    // Keep the panes that already show a run of this experiment; drop the rest.
    while (static_cast<int>(followers_.size()) > wanted) {
        auto& follower = followers_.back();
        follower->pane->player()->pause();
        delete follower->pane;
        delete follower->session;
        followers_.pop_back();
    }
    for (int i = 0; i < wanted; ++i) {
        if (i >= static_cast<int>(followers_.size())) {
            auto follower = std::make_unique<Follower>();
            follower->session = new Session(this);
            auto* player = new ReplayPlayer(follower->session, queue_, nullptr);
            if (const auto kind = player_->engineKind())
                player->setEngine(*kind);
            player->setOverlayVisible(false);
            follower->pane = new RunPane(follower->session, player, this);
            Follower* raw = follower.get();
            connect(follower->pane, &RunPane::runChosen, this, [this, raw](const QString& directory) {
                loadInto(*raw, directory);
            });
            followers_.push_back(std::move(follower));
        }
        Follower& follower = *followers_[static_cast<std::size_t>(i)];
        // A pane keeps its run if it is still in the experiment and not the open run.
        bool keep = !follower.directory.isEmpty();
        if (keep) {
            keep = false;
            for (const BundleSummaryPtr& member : peers_) {
                if (absolute(member->directory) == absolute(follower.directory))
                    keep = true;
            }
            if (session_->run() && absolute(follower.directory) == absolute(session_->run()->info.bundleDirectory))
                keep = false;
        }
        if (!keep && i < choices.size())
            loadInto(follower, choices[i]);
        follower.pane->setChoices(peers_, follower.directory);
    }
    main_->setChoices({}, {});
    placePanes();
    updateBar();
    rebuildClock();
    if (followers_.empty())
        syncTimer_->stop();
    else
        syncTimer_->start();
    emit layoutChanged();
}

void CompareView::loadInto(Follower& follower, const QString& directory)
{
    if (absolute(directory) == absolute(follower.directory) && follower.session->run())
        return;
    follower.directory = directory;
    follower.held = false;
    follower.session->clear();
    if (!follower.watcher) {
        follower.watcher = new QFutureWatcher<LoadResult>(this);
        Follower* raw = &follower;
        connect(follower.watcher, &QFutureWatcherBase::finished, this, [this, raw] {
            const auto future = raw->watcher->future();
            std::ignore = raw->load.release();
            if (future.isCanceled() || future.resultCount() == 0)
                return;
            LoadResult result = future.result();
            raw->session->setRun(std::move(result.run), std::move(result.errors));
            raw->pane->setChoices(peers_, raw->directory);
            rebuildClock();
            sync();
            if (isReady())
                emit ready();
        });
    }
    auto future = loadBundle(directory);
    follower.load.reset(future);
    follower.watcher->setFuture(future);
}

void CompareView::rebuildClock()
{
    std::vector<std::shared_ptr<const RunData>> runs{session_->run()};
    for (const auto& follower : followers_)
        runs.push_back(follower->session->run());
    clock_ = ComparisonClock(std::move(runs), alignment_);
}

double CompareView::masterTime() const
{
    // The first pane leads: its own engine sets the pace, so a slow decoder slows the master
    // time instead of making the first pane skip.
    const ReplaySource* engine = player_->engine();
    const double replay = engine && engine->isReady() ? engine->position()
                                                      : session_->clock().simToReplay(session_->cursor()->time());
    return clock_.masterOf(0, replay);
}

double CompareView::steer(int index, double master, bool running, bool& held)
{
    RunPane* target = pane(index);
    ReplayPlayer* player = target->player();
    const ReplaySource* engine = player->engine();
    Session* session = target->session();
    if (!session->run() || index >= clock_.runCount() || !clock_.run(index) || !engine || !engine->isReady())
        return 0.0;
    const ComparisonClock::RunPosition position = clock_.positionOf(index, master);
    const double offset = engine->position() - position.replay;
    const auto moveTo = [&] { session->cursor()->setTime(position.sim, TimeCursor::Source::Program); };
    if (!running || position.ended) {
        held = false;
        if (player->isRunning())
            player->pause();
        if (!running && std::abs(offset) > kPausedSlackS)
            moveTo();
        return 0.0;
    }
    // Frozen in a stall (its own, or at the target), a pane is moved at once: the jump is not
    // seen because the picture does not change.
    const bool frozen = position.position.stalled
        || session->clock().positionAtReplay(engine->position()).stalled;
    if (offset < -(frozen ? kStalledBehindS : kBehindS)) {
        // Behind, usually while frozen in a stall: jump to where it should be.
        moveTo();
        held = false;
        if (!player->isRunning())
            player->play();
    } else if (offset > kAheadS) {
        // Ahead: wait for the master time.
        if (player->isRunning())
            player->pause();
        held = true;
    } else if (held ? offset < kCaughtUpS : !player->isRunning()) {
        held = false;
        player->play();
    }
    // A playing pane a little behind makes the first pane wait instead (see sync()).
    return frozen ? 0.0 : offset;
}

void CompareView::sync()
{
    if (followers_.empty() || !session_->run() || steering_)
        return;
    steering_ = true;
    const double master = masterTime();
    // Playing: the user's play, also while the first pane waits for a slower one.
    const bool running = player_->isRunning() || mainHeld_;
    double slowest = 0.0;
    for (std::size_t i = 0; i < followers_.size(); ++i)
        slowest = std::min(slowest, steer(static_cast<int>(i) + 1, master, running, followers_[i]->held));
    // Another pane playing slightly behind (a slower decoder): the first pane waits for it
    // rather than the other pane skipping ahead.
    if (running) {
        if (!mainHeld_ && slowest < -kAheadS && slowest >= -kBehindS) {
            mainHeld_ = true;
            player_->pause();
        } else if (mainHeld_ && slowest >= -kCaughtUpS) {
            mainHeld_ = false;
            player_->play();
        }
    } else {
        mainHeld_ = false;
    }
    steering_ = false;
}

void CompareView::placePanes()
{
    while (QLayoutItem* item = grid_->takeAt(0))
        delete item;
    const int count = paneCount();
    const int columns = count <= 1 ? 1 : 2;
    for (int i = 0; i < count; ++i)
        grid_->addWidget(pane(i), i / columns, i % columns);
    for (int c = 0; c < 2; ++c)
        grid_->setColumnStretch(c, c < columns ? 1 : 0);
    for (int r = 0; r < 2; ++r)
        grid_->setRowStretch(r, r < (count + columns - 1) / columns ? 1 : 0);
}

void CompareView::updateBar()
{
    bar_->setVisible(peers_.size() >= 2);
    {
        const QSignalBlocker block(layoutSwitch_);
        layoutSwitch_->setCurrentIndex(static_cast<int>(layout_));
        layoutSwitch_->setSegmentEnabled(2, peers_.size() >= 3);
    }
    alignmentSwitch_->setVisible(layout_ != Layout::Single);
    main_->setHeaderVisible(layout_ != Layout::Single);
}

} // namespace abrviz
