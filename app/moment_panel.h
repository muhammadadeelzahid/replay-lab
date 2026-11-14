#pragma once

/**
 * @file
 * @brief The inspector beside the replay that describes the moment at the cursor.
 */

#include <QWidget>

class QGridLayout;
class QLabel;

namespace abrviz {

class ReplayPlayer;
class Session;

/**
 * @brief The inspector beside the replay: what the simulated viewer saw at the cursor.
 *
 * It shows the segment, rung, resolution, bitrate, buffer and stall state at the cursor, the
 * policy's last decision before it, the network as last measured, and the engine and times.
 * It has a fixed width, so its changing values never relayout the window.
 */
class MomentPanel final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Builds the panel and starts following the session and the player.
     *
     * The values refresh when the cursor moves, the run changes, or the player's state or
     * engine changes.
     *
     * @param session The session whose run and cursor are shown; not owned.
     * @param player The player whose engine is shown; not owned.
     * @param parent The parent widget.
     */
    MomentPanel(Session* session, ReplayPlayer* player, QWidget* parent = nullptr);

    /**
     * @brief The value shown for a field, for tests.
     *
     * @param field The field's row title, e.g. "Segment" or "Buffer".
     * @return The label's text ("–" when unknown, possibly rich text); empty if there is no
     *         such field.
     */
    [[nodiscard]] QString value(const QString& field) const;

private:
    /**
     * @brief Recomputes every value for the cursor's time.
     *
     * Every value is reset to "–" first, and only the values the run has data for are set.
     */
    void refresh();

    /**
     * @brief Appends a bold section heading spanning both grid columns.
     *
     * @param title The heading text.
     */
    void addHeading(const QString& title);

    /**
     * @brief Appends a row with a secondary-text title and a selectable value label.
     *
     * The value label ignores its size hint, so long values are cut rather than widening the
     * panel. The row is registered for value().
     *
     * @param name The row title.
     * @return The value label, owned by the panel.
     */
    QLabel* addRow(const QString& name);

    Session* session_;      ///< Not owned.
    ReplayPlayer* player_;  ///< Not owned.
    QLabel* segment_;       ///< Segment on screen and the segment count, or the startup time.
    QLabel* quality_;       ///< Rung on screen, with its colour and resolution.
    QLabel* bitrate_;       ///< Bitrate of the rung on screen.
    QLabel* buffer_;        ///< Buffer level at the cursor.
    QLabel* status_;        ///< Player state at the cursor, such as playing or stalled.
    QLabel* decisionTime_;  ///< Time of the last decision and the segment it was for.
    QLabel* chosen_;        ///< Rung chosen by the last decision.
    QLabel* feeder_;        ///< Feeder link rate and the number of UEs sharing it.
    QLabel* satellite_;     ///< Satellite altitude.
    QLabel* carrier_;       ///< Carrier frequency and bandwidth.
    QLabel* relay_;         ///< IAB relay distance to the viewer, or the relay count.
    QLabel* estimate_;      ///< Throughput estimate as last measured or used by a decision.
    QLabel* rtt_;           ///< Round-trip time as last measured.
    QLabel* engine_;        ///< Name of the replay engine.
    QLabel* simTime_;       ///< Cursor time in simulation seconds.
    QLabel* replayTime_;    ///< Cursor time on the replay timeline, and its length.
    QList<QPair<QString, QLabel*>> fields_;  ///< Row title and value label, in display order.
    QGridLayout* grid_ = nullptr;            ///< One grid for all sections, so values line up.
};

} // namespace abrviz
