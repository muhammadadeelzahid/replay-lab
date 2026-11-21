#pragma once

/**
 * @file
 * @brief Two runs in one frame, divided by a line that can be dragged.
 */

#include <QGraphicsView>

class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QGraphicsVideoItem;
class QVideoSink;

namespace abrviz {

/**
 * @brief Shows one run's video on the left of a divider and another's on the right, in the
 *        same frame, with a round 2× loupe that follows the pointer.
 *
 * The view owns two video items; the engines of the two runs send their frames to
 * `leftSink()` and `rightSink()` while the view is shown. The divider is dragged with the
 * mouse or moved with the left and right arrow keys (Shift for bigger steps).
 */
class WipeView final : public QGraphicsView {
    Q_OBJECT

public:
    /**
     * @brief Creates the view.
     *
     * @param parent The parent widget.
     */
    explicit WipeView(QWidget* parent = nullptr);

    /// @return The sink of the left video.
    [[nodiscard]] QVideoSink* leftSink() const;

    /// @return The sink of the right video.
    [[nodiscard]] QVideoSink* rightSink() const;

    /**
     * @brief Sets the names shown in the top corners.
     *
     * @param left The left run's label, e.g. "LLM · 2160p · VMAF 92".
     * @param right The right run's label.
     * @param leftColor The left run's algorithm colour.
     * @param rightColor The right run's algorithm colour.
     */
    void setLabels(const QString& left, const QString& right, const QColor& leftColor, const QColor& rightColor);

    /**
     * @brief Moves the divider.
     *
     * @param fraction Where the divider is, from 0 (left edge) to 1 (right edge); clamped.
     */
    void setDivider(double fraction);

    /// @return Where the divider is, from 0 to 1.
    [[nodiscard]] double divider() const { return divider_; }

    /// @return Whether the loupe is shown.
    [[nodiscard]] bool isLoupeVisible() const;

protected:
    /// Keeps the videos filling the view.
    void resizeEvent(QResizeEvent* event) override;
    /// Starts a drag of the divider.
    void mousePressEvent(QMouseEvent* event) override;
    /// Drags the divider, and moves the loupe.
    void mouseMoveEvent(QMouseEvent* event) override;
    /// Ends a drag.
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// Hides the loupe.
    void leaveEvent(QEvent* event) override;
    /// Moves the divider with the arrow keys.
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// Lays out the videos, the clip, the divider and the labels for the view's size.
    void layoutScene();

    QGraphicsScene* scene_;            ///< Holds every item.
    QGraphicsVideoItem* left_;         ///< The left run, full size.
    QGraphicsRectItem* clip_;          ///< Clips the right run to the right of the divider.
    QGraphicsVideoItem* right_;        ///< The right run, full size, inside the clip.
    QGraphicsRectItem* line_;          ///< The divider.
    QGraphicsRectItem* leftTag_;       ///< Background of the left label.
    QGraphicsSimpleTextItem* leftText_;   ///< The left label.
    QGraphicsRectItem* rightTag_;      ///< Background of the right label.
    QGraphicsSimpleTextItem* rightText_;  ///< The right label.
    QGraphicsView* loupe_;             ///< The 2× round view of the same scene.
    double divider_ = 0.5;             ///< Where the divider is, 0 to 1.
    bool dragging_ = false;            ///< A drag of the divider is in progress.
};

} // namespace abrviz
