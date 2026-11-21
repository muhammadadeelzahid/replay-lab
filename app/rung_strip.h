#pragma once

/**
 * @file
 * @brief A thin strip of the rungs a run played, under its pane's header.
 */

#include <QWidget>

namespace abrviz {

class Session;

/**
 * @brief The rung on screen over the whole session, one colour per rung (as in the segment
 *        timeline), stalls in red and a line at the cursor.
 *
 * The strips of compared runs share one time range, so their rung choices line up. Hovering
 * shows the segment and its quality.
 */
class RungStrip final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the strip for a session.
     *
     * @param session The pane's session; it outlives the strip.
     * @param parent The parent widget.
     */
    explicit RungStrip(Session* session, QWidget* parent = nullptr);

    /**
     * @brief Sets the end of the time axis, the same for every compared run.
     *
     * @param end Simulation time in seconds; 0 uses the run's own end.
     */
    void setTimeRange(double end);

    /// @return The time at the right edge, in seconds of simulation time.
    [[nodiscard]] double timeRange() const;

    /// @return A fixed height and any width.
    [[nodiscard]] QSize sizeHint() const override;

protected:
    /// Draws the rungs, stalls and cursor.
    void paintEvent(QPaintEvent* event) override;
    /// Shows the segment under the pointer as a tool tip.
    bool event(QEvent* event) override;

private:
    /**
     * @brief The x position of a time.
     *
     * @param time Simulation time in seconds.
     * @return The x coordinate in the widget.
     */
    [[nodiscard]] double xAt(double time) const;

    Session* session_;  ///< Not owned.
    double end_ = 0.0;  ///< Shared end of the axis; 0 for the run's own.
};

} // namespace abrviz
