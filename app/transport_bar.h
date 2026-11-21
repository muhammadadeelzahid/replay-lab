#pragma once

/**
 * @file
 * @brief The playback strip under the views of an open run.
 */

#include "abrviz/media/replay_source.h"

#include <QWidget>

class QAction;
class QLabel;
class QToolButton;

namespace abrviz {

class ReplayPlayer;
class Session;

/**
 * @brief The strip under every view while a run is open, styled after QuickTime Player.
 *
 * It shows the elapsed replay time, borderless playback buttons, the state and remaining time,
 * and the playback bar (a Scrubber), between the time played and the time left.
 *
 * The buttons show the window's actions (so the menu, the shortcuts and the buttons are the
 * same commands). The labels have fixed widths: they change many times a second while the
 * replay runs, and a label that grew would lay out (and repaint) the whole window.
 */
class TransportBar final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief The window's actions that the playback buttons show. None is owned.
     */
    struct Actions {
        QAction* previousSegment = nullptr;  ///< Moves to the previous segment.
        QAction* stepBack = nullptr;         ///< Steps the replay back.
        QAction* play = nullptr;             ///< Toggles play and pause.
        QAction* stepForward = nullptr;      ///< Steps the replay forward.
        QAction* nextSegment = nullptr;      ///< Moves to the next segment.
        QAction* watch = nullptr;            ///< Full screen with the video alone; optional.
    };

    /**
     * @brief Builds the bar and starts following the session and the player.
     *
     * The labels update when the cursor moves, the run changes, or the player's state or
     * engine changes.
     *
     * @param session The session whose run and cursor are shown; not owned.
     * @param player The player whose engine state is shown; not owned.
     * @param scrubber The playback bar, added to the bar's layout (and so reparented).
     * @param actions The actions the playback buttons show.
     * @param parent The parent widget.
     */
    TransportBar(Session* session, ReplayPlayer* player, QWidget* scrubber,
                 const Actions& actions, QWidget* parent = nullptr);

    /**
     * @brief The elapsed and remaining replay time, for tests.
     *
     * @return Both labels joined by a space, e.g. "1:05.3 -2:10.0"; " " without a run.
     */
    [[nodiscard]] QString timeText() const;

    /**
     * @brief The state label, for tests.
     *
     * @return The engine state and the simulation time at the cursor, as "<state> · sim <time>"
     *         (rich text when stalled or in error); empty without a run.
     */
    [[nodiscard]] QString stateText() const;

private:
    /**
     * @brief Refreshes the time and state labels from the cursor and the engine.
     *
     * Clears every label when there is no run. A stalled or failed engine is shown in red.
     */
    void update();

    Session* session_;      ///< Not owned.
    ReplayPlayer* player_;  ///< Not owned.
    QLabel* time_;       ///< Elapsed replay time.
    QLabel* remaining_;  ///< Remaining replay time.
    QLabel* state_;      ///< State and simulation time.
};

} // namespace abrviz
