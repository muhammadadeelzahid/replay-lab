#pragma once

/**
 * @file
 * @brief The diagram of the network path a replay's frames took.
 */

#include "abrviz/core/model/run_data.h"

#include <QWidget>

#include <memory>

class QTimer;

namespace abrviz {

class ReplayPlayer;
class Session;

/**
 * @brief The path every frame of the replay took, drawn across the top of the Replay view.
 *
 *     video server ── feeder link ──> LEO satellite ── satellite backhaul ──> IAB relay ── 5G access ──> viewer
 *
 * The diagram carries the numbers of the open run (feeder rate, satellite altitude, carrier,
 * relay distance). It follows the cursor: while a segment is downloading the links carry
 * moving dashes and the segment is named; during a stall the viewer is marked red. Without a
 * run it shows the same path without numbers (the welcome screen uses it that way).
 */
class NetworkPathView final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the view and, if given a session and player, starts following them.
     *
     * The view refreshes when the run changes, the cursor moves, or the player's state changes.
     *
     * @param session The session whose run and cursor are shown; not owned. May be null for
     *                the static diagram.
     * @param player The player whose running state drives the animation; not owned. May be
     *               null for the static diagram.
     * @param parent The parent widget.
     */
    NetworkPathView(Session* session, ReplayPlayer* player, QWidget* parent = nullptr);

    /**
     * @brief The preferred size.
     *
     * @return 900 x 150 pixels.
     */
    QSize sizeHint() const override;

    /**
     * @brief The smallest useful size.
     *
     * @return 520 x 150 pixels.
     */
    QSize minimumSizeHint() const override;

    /**
     * @brief What the strip says at the cursor, for tests.
     *
     * @return The activity line (e.g. which segment is downloading, or a stall); empty
     *         without a run.
     */
    [[nodiscard]] QString activityText() const { return activity_; }

    /**
     * @brief The labels of the diagram, for tests and for painting.
     *
     * @return Seven labels in path order: the four nodes and the three links between them,
     *         alternating, with the run's numbers appended where it has them.
     */
    [[nodiscard]] QStringList labels() const;

    /**
     * @brief Whether a segment download spans the cursor.
     *
     * @return True if a segment was in flight at the cursor's time.
     */
    [[nodiscard]] bool isDownloading() const { return downloading_; }

protected:
    /**
     * @brief Paints the band, the links, the nodes and the activity line.
     *
     * Links are drawn in the accent colour with moving dashes while a segment is downloading;
     * the viewer node is red during a stall.
     *
     * @param event The paint event (unused; the whole widget is painted).
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief Restarts the dash animation if a download is running.
     *
     * @param event The show event.
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Stops the dash animation while the view is hidden.
     *
     * @param event The hide event.
     */
    void hideEvent(QHideEvent* event) override;

private:
    /**
     * @brief Recomputes the run, the download and stall state and the activity line for the
     *        cursor's time, then updates the animation and repaints.
     */
    void refresh();

    /**
     * @brief Runs the dash animation only while the view is visible, a segment is downloading
     *        and the player is running; stops it otherwise.
     */
    void updateAnimation();

    Session* session_;      ///< Not owned; may be null.
    ReplayPlayer* player_;  ///< Not owned; may be null.
    QTimer* animation_;     ///< Advances phase_ about 30 times a second.
    std::shared_ptr<const RunData> run_;  ///< The run shown; null if none.
    QString activity_;      ///< What the stream is doing at the cursor; empty if no run.
    bool downloading_ = false;  ///< Whether a segment download spans the cursor.
    bool stalled_ = false;  ///< Whether playback is stalled at the cursor.
    bool running_ = false;  ///< Whether the player is running.
    double phase_ = 0.0;    ///< Dash offset of the links, in pixels.
};

} // namespace abrviz
