#pragma once

/**
 * @file
 * @brief The segment timeline: downloads, playback and ABR decisions of the open run as lanes.
 */

#include "abrviz/core/model/run_data.h"

#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

namespace abrviz {

class Session;

/**
 * @brief A painted strip over simulation time with three lanes.
 *
 * The lanes show downloads (and the waits while the buffer was full), segments on screen (and
 * stalls), and ABR decisions (with guard overrides marked). Bars are coloured by rung, and each
 * mark has a tooltip. Clicking or dragging moves the cursor (Source::Timeline). The widget
 * repaints on every cursor move, including its own, since it draws the cursor line.
 */
class SegmentTimeline final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief The lanes, top to bottom; the value is the lane's row.
     */
    enum class Lane { Downloads, Playback, Decisions };
    /**
     * @brief What a mark shows.
     *
     * CutOff, Decision and GuardOverride are point marks; the others are bars.
     */
    enum class Kind { Download, BufferFullWait, Segment, Stall, CutOff, Decision, GuardOverride };

    /**
     * @brief One drawn mark.
     *
     * Built when the run changes; painting and tooltips read the list.
     */
    struct Item {
        Lane lane = Lane::Downloads;  ///< Lane the mark is drawn in.
        Kind kind = Kind::Download;  ///< What the mark shows; sets its shape and colour.
        double start = 0.0;   ///< Simulation time in seconds.
        double end = 0.0;     ///< Equal to `start` for point marks.
        int rung = -1;        ///< -1 when the mark has no rung colour.
        bool dimmed = false;  ///< Drawn faded (a download that was never shown).
        QString text;         ///< Tooltip.
    };

    /**
     * @brief Creates the timeline and builds the marks of the current run.
     *
     * @param session Session to read from; not owned, must outlive the widget.
     * @param parent Parent widget, or null.
     */
    explicit SegmentTimeline(Session* session, QWidget* parent = nullptr);

    /**
     * @brief The marks of the open run, in drawing order.
     *
     * @return The marks; empty without a run.
     */
    [[nodiscard]] const std::vector<Item>& items() const { return items_; }
    /**
     * @brief Number of marks of one kind.
     *
     * @param kind Kind of mark to count.
     * @return The number of marks of that kind.
     */
    [[nodiscard]] int count(Kind kind) const;

    /**
     * @brief Maps a simulation time to a widget x coordinate.
     *
     * @param time Simulation time in seconds.
     * @return The x coordinate; the left edge of the plot area if the run has no length.
     * @see timeAt()
     */
    [[nodiscard]] double xAt(double time) const;
    /**
     * @brief Maps a widget x coordinate to a simulation time.
     *
     * @param x The x coordinate in widget pixels.
     * @return Simulation time in seconds, clamped to [0, end of the time axis].
     * @see xAt()
     */
    [[nodiscard]] double timeAt(double x) const;
    /**
     * @brief The mark under a position.
     *
     * Prefers point marks and stalls over the bars beneath them: marks drawn later (on top) win.
     * Marks are hit one pixel wider on each side.
     *
     * @param pos Position in widget coordinates.
     * @return The mark, or null if there is none at `pos`.
     */
    [[nodiscard]] const Item* itemAt(QPointF pos) const;

    /**
     * @brief Preferred size: 800 pixels wide, as tall as the minimum.
     *
     * @return The preferred size.
     */
    QSize sizeHint() const override;
    /**
     * @brief Smallest size that fits the lane labels, the three lanes and the time axis.
     *
     * @return The minimum size.
     */
    QSize minimumSizeHint() const override;

protected:
    /**
     * @brief Shows the tooltip of the mark under the mouse, or hides it over empty space.
     *
     * @param event The event; tooltip events are handled here, others go to QWidget.
     * @return True if the event was handled.
     */
    bool event(QEvent* event) override;
    /**
     * @brief Paints the lane labels, time axis, marks and cursor line.
     *
     * @param event The paint event (unused).
     */
    void paintEvent(QPaintEvent* event) override;
    /**
     * @brief Moves the cursor to the time under a left-button press.
     *
     * @param event The mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;
    /**
     * @brief Moves the cursor with the mouse while the left button is held.
     *
     * @param event The mouse event.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    /**
     * @brief Takes the session's run and rebuilds the marks, then repaints.
     */
    void rebuild();
    /**
     * @brief Moves the cursor to the time at an x coordinate (Source::Timeline).
     *
     * Does nothing without a run.
     *
     * @param x The x coordinate in widget pixels.
     */
    void moveCursorTo(double x);
    /**
     * @brief Area of one lane.
     *
     * @param lane The lane.
     * @return The lane's rectangle in widget coordinates.
     */
    [[nodiscard]] QRectF laneRect(Lane lane) const;
    /**
     * @brief Area of all lanes, right of the labels and above the time axis.
     *
     * @return The rectangle in widget coordinates.
     */
    [[nodiscard]] QRectF plotRect() const;
    /**
     * @brief Area a mark is drawn in.
     *
     * Point marks are 7 pixels wide, buffer-full waits fill the lower third of their lane, and
     * stalls are at least 4 pixels wide so short ones stay visible.
     *
     * @param item The mark.
     * @return The rectangle in widget coordinates.
     */
    [[nodiscard]] QRectF itemRect(const Item& item) const;

    Session* session_;  ///< Not owned; outlives the widget.
    std::shared_ptr<const RunData> run_;  ///< The run shown; null if none.
    std::vector<Item> items_;  ///< Marks of the run, in painting order.
    double endTime_ = 0.0;  ///< End of the time axis in seconds (Session::endTime()).
};

} // namespace abrviz
