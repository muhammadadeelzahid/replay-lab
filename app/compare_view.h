#pragma once

/**
 * @file
 * @brief The Replay page's video area: one run, or several runs side by side in sync.
 */

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/load_result.h"
#include "abrviz/core/playback/comparison_clock.h"
#include "abrviz/core/util/future_guard.h"

#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QGridLayout;
class QTimer;

namespace abrviz {

class RenderQueue;
class ReplayPlayer;
class RunPane;
class SegmentedControl;
class Session;
class WipeView;

/**
 * @brief Shows the open run alone, as before, or with the other ABR algorithms of its
 *        experiment (same video, feeder rate and run number) side by side.
 *
 * Layouts: Single (the open run), Side by side (2 panes), Grid (up to 4) and Wipe (2 runs in
 * one frame, see `WipeView`). The open run is
 * always the first pane; the window's transport, timeline, charts and inspector follow it. The
 * other panes each load their run, have their own engine, and follow the first pane through a
 * `ComparisonClock`, by session time (the same moment) or media time (the same frame).
 *
 * The first pane leads; its engine sets the pace. Every 100 ms the view compares each other
 * pane with where it should be. A pane frozen in a stall is moved forward at once (unseen, as
 * its picture does not change); a pane that is ahead waits; when a playing pane falls a little
 * behind (a slower decoder), the first pane waits for it; only a pane far behind is moved. In media time the
 * panes then stay within a frame or two. In session time a playing run moves through the
 * simulation faster than a stalled one (each 3.3 s simulated segment plays as 2 s of video),
 * so while one pane is stalled and another plays, the playing one waits in short steps. The bar with the layout and alignment controls is shown only when the
 * open run has other algorithms to compare with.
 */
class CompareView final : public QWidget {
    Q_OBJECT

public:
    /** @brief How many runs are shown. */
    enum class Layout { Single, SideBySide, Grid, Wipe };

    /**
     * @brief Creates the view around the window's own player.
     *
     * @param session The window's session (the open run); not owned.
     * @param player The window's player; reparented into the first pane.
     * @param queue The render queue for the other panes' pre-rendered engines; not owned.
     * @param parent The parent widget.
     */
    CompareView(Session* session, ReplayPlayer* player, RenderQueue* queue, QWidget* parent = nullptr);

    /// Stops the other panes' loads and engines.
    ~CompareView() override;

    /**
     * @brief Tells the view which runs exist, to find the open run's experiment.
     *
     * @param bundles Every run of the library.
     */
    void setLibrary(const QList<BundleSummaryPtr>& bundles);

    /**
     * @brief Shows one, two or up to four runs.
     *
     * Side by side and Grid need other runs in the open run's experiment; without them the
     * view stays Single. The choice is kept and applied to every run opened later that has
     * other algorithms to compare with.
     *
     * @param layout The layout.
     */
    void setLayoutMode(Layout layout);

    /**
     * @brief Shows a run side by side with the open run (the Side by side layout, unless more
     *        panes are shown already), in the second pane.
     *
     * @param directory The run's bundle directory; it must be in the open run's experiment.
     */
    void compareWith(const QString& directory);

    /// @return The layout shown.
    [[nodiscard]] Layout layoutMode() const { return layout_; }

    /**
     * @brief Lines the panes up by session time or by media time.
     *
     * @param alignment The alignment.
     */
    void setAlignment(ComparisonClock::Alignment alignment);

    /// @return The alignment.
    [[nodiscard]] ComparisonClock::Alignment alignment() const { return alignment_; }

    /// @return The runs of the open run's experiment (empty if it is the only one).
    [[nodiscard]] const QList<BundleSummaryPtr>& experimentRuns() const { return peers_; }

    /// @return The number of panes shown.
    [[nodiscard]] int paneCount() const;

    /**
     * @brief Returns a pane.
     *
     * @param index 0 for the open run, then the others.
     * @return The pane, or null if `index` is out of range.
     */
    [[nodiscard]] RunPane* pane(int index) const;

    /**
     * @brief How far a pane is from where it should be, for `--profile` and tests.
     *
     * @param index A pane other than the first.
     * @return The pane's replay time minus its target, in seconds; 0 if it has no run yet.
     */
    [[nodiscard]] double drift(int index) const;

    /**
     * @brief How far the frame a pane shows is from the frame it should show, for `--profile`.
     *
     * Uses the media time of the frames on screen, when the engine reports it (the GStreamer
     * engine does), which is exact; otherwise falls back to drift().
     *
     * @param index A pane other than the first.
     * @return The pane's frame media time minus the expected one, in seconds.
     */
    [[nodiscard]] double frameDrift(int index) const;

    /**
     * @brief Tells whether the first pane and another are both playing a segment now (not
     *        starting, stalled or ended), so that their drift says how well they are in sync.
     *
     * @param index A pane other than the first.
     * @return True if both show a playing segment at the master time.
     */
    [[nodiscard]] bool bothPlaying(int index) const;

    /**
     * @brief Returns the runs shown next to the open run.
     *
     * @return The other panes' runs that have loaded, in pane order.
     */
    [[nodiscard]] std::vector<std::shared_ptr<const RunData>> otherRuns() const;

    /// @return The Wipe view (shown in the Wipe layout).
    [[nodiscard]] WipeView* wipeView() const { return wipe_; }

    /// @return Whether every other pane has loaded its run and its engine shows a picture.
    [[nodiscard]] bool isReady() const;

signals:
    /// The layout or the panes' runs changed.
    void layoutChanged();

    /// Every other pane has loaded its run.
    void ready();

    /// The runs shown next to the open run changed (see otherRuns()).
    void runsChanged();

private:
    /// One pane other than the first, with its own session, player and load.
    struct Follower {
        Session* session = nullptr;   ///< child of the view
        RunPane* pane = nullptr;      ///< child of the view; owns the player
        QString directory;            ///< the run shown or being loaded
        FutureGuard<LoadResult> load;            ///< the pending load
        QFutureWatcher<LoadResult>* watcher = nullptr;  ///< reports the load
        bool held = false;            ///< paused because it was ahead
        bool lining = false;          ///< jumped to the master time, its frame not shown yet
        QElapsedTimer liningClock;    ///< since the jump
    };

    /// Covers each other pane while it loads, or after a jump until its frame is right.
    void updateCovers();

    /**
     * @brief How far a pane's frame is from the frame it should show at the master time.
     *
     * @param index A pane other than the first.
     * @return Media seconds; 0 if unknown.
     */
    [[nodiscard]] double frameOffset(int index) const;

    /// Finds the open run's experiment and rebuilds the panes.
    void mainRunChanged();

    /// Creates or removes panes for the layout and gives them their runs.
    void rebuildPanes();

    /// Starts loading a run into a pane.
    void loadInto(Follower& follower, const QString& directory);

    /// Rebuilds the comparison clock from the panes' runs.
    void rebuildClock();

    /// Moves or holds the panes so that they follow the master time.
    void sync();

    /// @return The master time now: the first pane's position.
    [[nodiscard]] double masterTime() const;

    /**
     * @brief Moves or holds one pane.
     *
     * @param index The pane.
     * @param master The master time.
     * @param running Whether the comparison is playing.
     * @param held Whether the pane is waiting; updated.
     * @return How far the pane is from its target, in seconds, while it plays a segment; 0
     *         otherwise.
     */
    double steer(int index, double master, bool running, bool& held);

    /// Places the panes in the grid, or the Wipe view.
    void placePanes();

    /// Sends the first two runs' frames to the Wipe view while it is shown, to their own
    /// players otherwise.
    void routeVideo();

    /// Updates the Wipe view's labels from the panes.
    void updateWipeLabels();


    /// Shows the bar and its state.
    void updateBar();

    /// @return The runs to show next to the open run, in order, for `count` other panes.
    [[nodiscard]] QStringList defaultFollowers(int count) const;

    Session* session_;              ///< Not owned.
    ReplayPlayer* player_;          ///< The window's player, in the first pane.
    RenderQueue* queue_;            ///< Not owned.
    RunPane* main_;                 ///< The first pane.
    std::vector<std::unique_ptr<Follower>> followers_;  ///< The other panes.
    QWidget* bar_;                  ///< Layout and alignment controls.
    SegmentedControl* layoutSwitch_;     ///< Single, Side by side, Grid.
    SegmentedControl* alignmentSwitch_;  ///< Same moment, Same frame.
    QGridLayout* grid_;             ///< Holds the panes.
    WipeView* wipe_;                ///< Shown instead of the panes in the Wipe layout.
    bool routedToWipe_ = false;     ///< The engines send their frames to wipe_.
    QTimer* syncTimer_;             ///< Calls sync() every 100 ms.
    QList<BundleSummaryPtr> library_;    ///< Every run of the library.
    QList<BundleSummaryPtr> peers_;      ///< The open run's experiment, LLM first.
    ComparisonClock clock_;         ///< First pane, then the others.
    Layout layout_ = Layout::Single;
    Layout preferred_ = Layout::Single;  ///< The layout last chosen; used when there are runs to compare.
    ComparisonClock::Alignment alignment_ = ComparisonClock::Alignment::SessionTime;
    bool steering_ = false;  ///< Set while the view itself moves or pauses a pane.
    bool mainHeld_ = false;  ///< The first pane is waiting for a slower one.
};

} // namespace abrviz
