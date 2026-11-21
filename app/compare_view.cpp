#include "compare_view.h"

#include "replay_player.h"
#include "run_pane.h"
#include "segmented_control.h"
#include "session.h"
#include "time_cursor.h"
#include "wipe_view.h"
#include "algorithm_colors.h"

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
    case CompareView::Layout::Wipe: return std::min(2, available);
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
    , wipe_(new WipeView(this))
    , syncTimer_(new QTimer(this))
{
    setObjectName(QStringLiteral("compareView"));
    layoutSwitch_->addSegment(tr("Single"), tr("The open run alone"));
    layoutSwitch_->addSegment(tr("Side by side"), tr("The open run and one other algorithm"));
    layoutSwitch_->addSegment(tr("Grid"), tr("Up to four algorithms"));
    layoutSwitch_->addSegment(tr("Wipe"), tr("Two algorithms in one frame, divided by a line you can drag; "
                                             "a loupe follows the pointer"));
    wipe_->hide();
    connect(player_, &ReplayPlayer::engineChanged, this, [this] {
        routedToWipe_ = false;
        routeVideo();
    });
    connect(main_, &RunPane::summaryChanged, this, &CompareView::updateWipeLabels);
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

void CompareView::preferFollower(const QString& directory)
{
    preferred_follower_ = directory;
}

void CompareView::compareWith(const QString& directory)
{
    if (layout_ == Layout::Single)
        setLayoutMode(Layout::SideBySide);
    if (!followers_.empty())
        loadInto(*followers_.front(), directory);
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

double CompareView::frameOffset(int index) const
{
    if (index < 1 || index >= paneCount() || index >= clock_.runCount() || !clock_.run(index))
        return 0.0;
    const ReplaySource* engine = pane(index)->player()->engine();
    const QVariant frame = engine ? engine->property("frameMediaTime") : QVariant();
    if (!frame.isValid())
        return 0.0;
    // Where this pane should be at the master time, whatever the first pane shows (it may be
    // frozen in a stall).
    const double expected = clock_.clock(index).replayToMedia(clock_.positionOf(index, masterTime()).replay);
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
    // A run asked for with preferFollower() comes first (once).
    if (!preferred_follower_.isEmpty() && absolute(preferred_follower_) != open
        && std::ranges::any_of(peers_, [&](const BundleSummaryPtr& m) {
               return absolute(m->directory) == absolute(preferred_follower_);
           }))
        directories << preferred_follower_;
    for (const BundleSummaryPtr& member : peers_) {
        if (directories.size() >= count)
            break;
        if (absolute(member->directory) != open && !directories.contains(member->directory))
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
    if (session_->run())
        preferred_follower_.clear();  // used for the run it was set for

    // Keep the panes that already show a run of this experiment; drop the rest.
    if (routedToWipe_) {
        // Give every engine back its own player's picture before panes go.
        const Layout keep = layout_;
        layout_ = Layout::Single;
        routeVideo();
        layout_ = keep;
    }
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
            connect(player, &ReplayPlayer::engineChanged, this, [this] {
                routedToWipe_ = false;
                routeVideo();
            });
            connect(follower->pane, &RunPane::summaryChanged, this, &CompareView::updateWipeLabels);
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
    // Already showing it, or still loading it.
    if (absolute(directory) == absolute(follower.directory) && (follower.session->run() || follower.load.isRunning()))
        return;
    follower.directory = directory;
    follower.held = false;
    follower.lining = false;
    follower.session->clear();
    follower.pane->setCover(tr("Loading…"));
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
    // The rung strips share one time axis, the longest of the runs shown.
    double end = 0.0;
    for (int i = 0; i < paneCount(); ++i)
        end = std::max(end, pane(i)->session()->run() ? pane(i)->session()->endTime() : 0.0);
    for (int i = 0; i < paneCount(); ++i)
        pane(i)->setTimeRange(end);
    emit runsChanged();
}

std::vector<std::shared_ptr<const RunData>> CompareView::otherRuns() const
{
    std::vector<std::shared_ptr<const RunData>> runs;
    for (const auto& follower : followers_) {
        if (follower->session->run())
            runs.push_back(follower->session->run());
    }
    return runs;
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
    const auto moveTo = [&] {
        session->cursor()->setTime(position.sim, TimeCursor::Source::Program);
        Follower& follower = *followers_[static_cast<std::size_t>(index - 1)];
        if (!follower.lining) {
            follower.lining = true;
            follower.liningClock.start();
        }
    };
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
    const PlaybackClock::Position here = session->clock().positionAtReplay(engine->position());
    const bool frozen = position.position.stalled || here.stalled;
    // Frozen in the same stall the master time is in: the picture is already right, and a seek
    // would only make the decoder start over (the engine plays the stall out by itself).
    const bool sameStall = here.stalled && position.position.stalled && here.segment == position.position.segment;
    if (sameStall) {
        held = false;
        if (!player->isRunning())
            player->play();
        return 0.0;
    }
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
    updateCovers();
    steering_ = false;
}

void CompareView::updateCovers()
{
    // A short jump is not covered (the frame arrives within a few ticks); a longer one, or one
    // the decoder is slow to finish, shows the card instead of a stale or broken picture.
    constexpr qint64 kShowAfterMs = 150;
    constexpr qint64 kGiveUpMs = 2000;
    constexpr double kLinedUpS = 0.08;  // two frames at 25 fps
    const QString leader = session_->run() ? algorithmDisplayName(session_->run()->info.policyName) : QString();
    for (std::size_t i = 0; i < followers_.size(); ++i) {
        Follower& follower = *followers_[i];
        const ReplaySource* engine = follower.pane->player()->engine();
        if (!follower.session->run() || !engine || !engine->isReady()) {
            // The pre-rendered engine renders the run's video first (once; it is cached).
            const bool rendering = engine && follower.session->run() && engine->state() == ReplaySource::State::Preparing;
            follower.pane->setCover(rendering ? tr("Rendering the video…") : tr("Loading…"));
            continue;
        }
        if (follower.lining) {
            const qint64 elapsed = follower.liningClock.elapsed();
            if (std::abs(frameOffset(static_cast<int>(i) + 1)) <= kLinedUpS || elapsed > kGiveUpMs)
                follower.lining = false;
            else if (elapsed > kShowAfterMs)
                follower.pane->setCover(tr("Lining up with %1…").arg(leader));
        }
        if (!follower.lining)
            follower.pane->setCover({});
    }
}

void CompareView::placePanes()
{
    while (QLayoutItem* item = grid_->takeAt(0))
        delete item;
    const bool wipe = layout_ == Layout::Wipe && !followers_.empty();
    const int count = paneCount();
    for (int i = 0; i < count; ++i)
        pane(i)->setVisible(!wipe);
    wipe_->setVisible(wipe);
    if (wipe) {
        grid_->addWidget(wipe_, 0, 0);
        grid_->setColumnStretch(0, 1);
        grid_->setColumnStretch(1, 0);
        grid_->setRowStretch(0, 1);
        grid_->setRowStretch(1, 0);
    } else {
        const int columns = count <= 1 ? 1 : 2;
        for (int i = 0; i < count; ++i)
            grid_->addWidget(pane(i), i / columns, i % columns);
        for (int c = 0; c < 2; ++c)
            grid_->setColumnStretch(c, c < columns ? 1 : 0);
        for (int r = 0; r < 2; ++r)
            grid_->setRowStretch(r, r < (count + columns - 1) / columns ? 1 : 0);
    }
    routeVideo();
    updateWipeLabels();
}

void CompareView::routeVideo()
{
    const bool wipe = layout_ == Layout::Wipe && !followers_.empty();
    if (wipe == routedToWipe_)
        return;
    ReplayPlayer* players[2] = {player_, followers_.empty() ? nullptr : followers_.front()->pane->player()};
    QVideoSink* wipeSinks[2] = {wipe_->leftSink(), wipe_->rightSink()};
    for (int i = 0; i < 2; ++i) {
        ReplayPlayer* player = players[i];
        ReplaySource* engine = player ? player->engine() : nullptr;
        if (!engine)
            continue;
        engine->setVideoSink(wipe ? wipeSinks[i] : player->videoSink());
        // Show the current frame in its new place at once.
        if (engine->isReady() && !player->isRunning())
            engine->seek(engine->position());
    }
    routedToWipe_ = wipe;
}

void CompareView::updateWipeLabels()
{
    if (layout_ != Layout::Wipe || followers_.empty())
        return;
    const RunPane* right = followers_.front()->pane;
    const auto key = [](const RunPane* pane) {
        return pane->session()->run() ? pane->session()->run()->info.policyName : QString();
    };
    wipe_->setLabels(main_->summary(), right->summary(), algorithmColor(key(main_), palette()),
                     algorithmColor(key(right), palette()));
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
