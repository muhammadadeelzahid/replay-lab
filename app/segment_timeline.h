#pragma once

#include "abrviz/core/model/run_data.h"

#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

namespace abrviz {

class Session;

/// A painted strip over simulation time with three lanes: downloads (and the waits while the
/// buffer was full), segments on screen (and stalls), and ABR decisions (with guard overrides
/// marked). Clicking or dragging moves the cursor.
class SegmentTimeline final : public QWidget {
    Q_OBJECT

public:
    enum class Lane { Downloads, Playback, Decisions };
    enum class Kind { Download, BufferFullWait, Segment, Stall, CutOff, Decision, GuardOverride };

    /// One drawn mark. Built when the run changes; painting and tooltips read the list.
    struct Item {
        Lane lane = Lane::Downloads;
        Kind kind = Kind::Download;
        double start = 0.0;
        double end = 0.0;  ///< equal to `start` for point marks
        int rung = -1;     ///< -1 when the mark has no rung colour
        bool dimmed = false;
        QString text;      ///< tooltip
    };

    explicit SegmentTimeline(Session* session, QWidget* parent = nullptr);

    [[nodiscard]] const std::vector<Item>& items() const { return items_; }
    [[nodiscard]] int count(Kind kind) const;

    /// Mapping between simulation time and widget x coordinates.
    [[nodiscard]] double xAt(double time) const;
    [[nodiscard]] double timeAt(double x) const;
    /// The mark under `pos`, preferring point marks and stalls over the bars beneath them.
    [[nodiscard]] const Item* itemAt(QPointF pos) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void rebuild();
    void moveCursorTo(double x);
    [[nodiscard]] QRectF laneRect(Lane lane) const;
    [[nodiscard]] QRectF plotRect() const;
    [[nodiscard]] QRectF itemRect(const Item& item) const;

    Session* session_;  // not owned; outlives the widget
    std::shared_ptr<const RunData> run_;
    std::vector<Item> items_;
    double endTime_ = 0.0;
};

} // namespace abrviz
