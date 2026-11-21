#pragma once

/**
 * @file
 * @brief A plain playback bar: how far the replay has played, with its stalls in red.
 */

#include <QWidget>

namespace abrviz {

class Session;

/**
 * @brief The replay as one thin, rounded bar, as in Apple's players.
 *
 * The part played so far is filled, the rest is grey, and the stalls (frozen frames in the
 * replay) are red. A knob marks the cursor. Clicking or dragging moves the cursor there; the
 * bar grows a little under the pointer, and a tool tip gives the time. It is laid out in replay
 * time, the time the transport bar shows.
 */
class Scrubber final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the bar for a session.
     *
     * @param session The open run and its cursor; outlives the bar.
     * @param parent The parent widget.
     */
    explicit Scrubber(Session* session, QWidget* parent = nullptr);

    /// @return A fixed height and any width.
    [[nodiscard]] QSize sizeHint() const override;

    /**
     * @brief The x position of a replay time, for tests.
     *
     * @param replay Replay time in seconds.
     * @return The x coordinate in the widget.
     */
    [[nodiscard]] double xAt(double replay) const;

    /**
     * @brief The stalls as replay-time intervals, for tests.
     *
     * @return Start and end of each stall in replay seconds, in time order.
     */
    [[nodiscard]] QList<QPair<double, double>> stallSpans() const;

protected:
    /// Draws the track, the played part, the stalls and the knob.
    void paintEvent(QPaintEvent* event) override;
    /// Moves the cursor to the pressed point.
    void mousePressEvent(QMouseEvent* event) override;
    /// Follows a drag; otherwise shows the time under the pointer.
    void mouseMoveEvent(QMouseEvent* event) override;
    /// Ends a drag.
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// Grows the bar under the pointer.
    void enterEvent(QEnterEvent* event) override;
    /// Shrinks it back.
    void leaveEvent(QEvent* event) override;

private:
    /**
     * @brief The replay time at an x position.
     *
     * @param x The x coordinate in the widget.
     * @return Replay time in seconds, within the replay.
     */
    [[nodiscard]] double replayAt(double x) const;

    /**
     * @brief Moves the cursor to the replay time at an x position.
     *
     * @param x The x coordinate in the widget.
     */
    void seekTo(double x);

    Session* session_;       ///< Not owned.
    bool hovered_ = false;   ///< The pointer is over the bar.
    bool dragging_ = false;  ///< A drag is moving the cursor.
};

} // namespace abrviz
