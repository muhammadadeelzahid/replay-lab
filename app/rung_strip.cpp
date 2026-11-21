#include "rung_strip.h"

#include "format.h"
#include "rung_colors.h"
#include "session.h"
#include "time_cursor.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace abrviz {

namespace {

constexpr int kHeight = 8;    ///< Height of the strip, in pixels.
constexpr int kMargin = 10;   ///< Space left and right, as the header's margins.

} // namespace

RungStrip::RungStrip(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setObjectName(QStringLiteral("rungStrip"));
    setFixedHeight(kHeight + 4);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMouseTracking(true);
    connect(session_, &Session::runChanged, this, qOverload<>(&QWidget::update));
    connect(session_->cursor(), &TimeCursor::timeChanged, this, qOverload<>(&QWidget::update));
}

void RungStrip::setTimeRange(double end)
{
    end_ = std::max(0.0, end);
    update();
}

double RungStrip::timeRange() const
{
    return end_ > 0.0 ? end_ : std::max(1.0, session_->endTime());
}

QSize RungStrip::sizeHint() const
{
    return {200, kHeight + 4};
}

double RungStrip::xAt(double time) const
{
    const double width = std::max(1, this->width() - 2 * kMargin);
    return kMargin + std::clamp(time / timeRange(), 0.0, 1.0) * width;
}

void RungStrip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRectF track(kMargin, 2, std::max(1, width() - 2 * kMargin), kHeight);
    QColor empty = palette().color(QPalette::Text);
    empty.setAlphaF(0.08F);
    painter.fillRect(track, empty);
    const auto& run = session_->run();
    if (!run)
        return;
    const int rungs = static_cast<int>(run->video.ladder.size());
    const double frame = run->info.playerSegmentDuration / kFramesPerSegment;
    for (const SegmentPlayback& p : run->playback) {
        const auto it = std::ranges::find(run->segments, p.segment, &Segment::index);
        if (it == run->segments.end())
            continue;
        const double left = xAt(p.firstFrameTime);
        const double right = xAt(p.lastFrameTime + frame);
        painter.fillRect(QRectF(left, track.top(), std::max(1.0, right - left), track.height()),
                         rungColor(it->playedRung, rungs));
    }
    const QColor stall = stallColor();
    for (const Stall& s : run->stalls) {
        const double left = xAt(s.start);
        painter.fillRect(QRectF(left, track.top(), std::max(1.5, xAt(s.end) - left), track.height()), stall);
    }
    // The cursor.
    const double x = xAt(session_->cursor()->time());
    painter.setPen(QPen(palette().color(QPalette::Text), 2));
    painter.drawLine(QPointF(x, 0), QPointF(x, height()));
}

bool RungStrip::event(QEvent* event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);
    const auto* help = static_cast<QHelpEvent*>(event);
    const auto& run = session_->run();
    if (!run) {
        QToolTip::hideText();
        return true;
    }
    const double width = std::max(1, this->width() - 2 * kMargin);
    const double time = std::clamp((help->pos().x() - kMargin) / width, 0.0, 1.0) * timeRange();
    const double frame = run->info.playerSegmentDuration / kFramesPerSegment;
    for (const SegmentPlayback& p : run->playback) {
        if (time < p.firstFrameTime || time > p.lastFrameTime + frame)
            continue;
        const auto it = std::ranges::find(run->segments, p.segment, &Segment::index);
        if (it == run->segments.end())
            break;
        QString text = tr("Segment %1 at %2").arg(p.segment).arg(format::rungLabel(run->video, it->playedRung));
        if (it->quality.vmaf)
            text += tr(", VMAF %1").arg(*it->quality.vmaf, 0, 'f', 0);
        QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    for (const Stall& s : run->stalls) {
        if (time >= s.start && time <= s.end) {
            QToolTip::showText(help->globalPos(), tr("Stalled %1 s").arg(s.end - s.start, 0, 'f', 2), this);
            return true;
        }
    }
    QToolTip::hideText();
    return true;
}

} // namespace abrviz
