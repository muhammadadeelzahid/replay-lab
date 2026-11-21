#pragma once

/**
 * @file
 * @brief One run's video in a comparison: a header with the algorithm and its state, over a
 *        player.
 */

#include "abrviz/core/load/bundle_library.h"

#include <QWidget>

class QComboBox;
class QLabel;

namespace abrviz {

class ReplayPlayer;
class Session;

/**
 * @brief A pane of the Replay page's side-by-side and grid layouts.
 *
 * The header shows the algorithm's name in its colour, the quality and VMAF on screen, the
 * buffer, and a red badge during a stall. A pane that can change its run has a menu of the
 * other runs of the experiment in its header.
 */
class RunPane final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates a pane around a player and its session.
     *
     * @param session The session whose run and cursor the pane shows; not owned.
     * @param player The player; reparented into the pane.
     * @param parent The parent widget.
     */
    RunPane(Session* session, ReplayPlayer* player, QWidget* parent = nullptr);

    /// @return The pane's session.
    [[nodiscard]] Session* session() const { return session_; }

    /// @return The pane's player.
    [[nodiscard]] ReplayPlayer* player() const { return player_; }

    /**
     * @brief Offers runs to choose from in the header; an empty list hides the menu.
     *
     * @param runs The runs.
     * @param current The directory of the run shown now.
     */
    void setChoices(const QList<BundleSummaryPtr>& runs, const QString& current);

    /**
     * @brief Hands the player back to its owner (the pane keeps nothing of it).
     *
     * @return The player, with no parent.
     */
    ReplayPlayer* takePlayer();

    /**
     * @brief Shows or hides the header (hidden when the pane is the only one).
     *
     * @param visible Whether the header is shown.
     */
    void setHeaderVisible(bool visible);

    /// @return The algorithm and what is on screen, as plain text, e.g. "LLM · 2160p · VMAF 92".
    [[nodiscard]] QString summary() const { return summary_; }

    /// @return The header's text, for tests: name, quality, buffer and stall state.
    [[nodiscard]] QString headerText() const;

signals:
    /// The header's text changed (run or cursor).
    void summaryChanged();

    /**
     * @brief The user chose another run in the header.
     *
     * @param directory The run's bundle directory.
     */
    void runChosen(const QString& directory);

private:
    /// Updates the header from the session's run and cursor.
    void refresh();

    Session* session_;           ///< Not owned.
    ReplayPlayer* player_;       ///< Child of the pane until takePlayer().
    QLabel* name_;               ///< The algorithm, in its colour.
    QComboBox* choice_;          ///< The other runs; hidden without choices.
    QLabel* status_;             ///< Quality, VMAF and buffer on screen.
    QLabel* stall_;              ///< Red badge during a stall.
    QWidget* header_;            ///< Holds the name, menu, badge and status.
    QString summary_;            ///< See summary().
};

} // namespace abrviz
