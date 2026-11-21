#include "scrubber.h"

#include "rung_colors.h"
#include "session.h"
#include "time_cursor.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr double kKnob = 7.0;     ///< Radius of the knob, in pixels.
constexpr double kTrack = 5.0;    ///< Height of the bar, in pixels.
constexpr double kHovered = 8.0;  ///< Height of the bar under the pointer, in pixels.
constexpr int kHeight = 22;       ///< Height of the widget, in pixels.

/// Replay time as "m:ss.s".
QString clockText(double seconds)
{
    seconds = std::round(std::max(0.0, seconds) * 10.0) / 10.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, 4, 'f', 1, QLatin1Char('0'));
}

} // namespace

Scrubber::Scrubber(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setObjectName(QStringLiteral("scrubber"));
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    connect(session_, &Session::runChanged, this, qOverload<>(&QWidget::update));
    connect(session_->cursor(), &TimeCursor::timeChanged, this, qOverload<>(&QWidget::update));
}

QSize Scrubber::sizeHint() const
{
    return {400, kHeight};
}

double Scrubber::xAt(double replay) const
{
    const double duration = session_->run() ? session_->clock().replayDuration() : 0.0;
    const double width = std::max(1.0, this->width() - 2.0 * kKnob);
    return kKnob + (duration > 0.0 ? std::clamp(replay / duration, 0.0, 1.0) : 0.0) * width;
}

double Scrubber::replayAt(double x) const
{
    const double duration = session_->run() ? session_->clock().replayDuration() : 0.0;
    const double width = std::max(1.0, this->width() - 2.0 * kKnob);
    return std::clamp((x - kKnob) / width, 0.0, 1.0) * duration;
}

QList<QPair<double, double>> Scrubber::stallSpans() const
{
    QList<QPair<double, double>> spans;
    const auto& run = session_->run();
    if (!run)
        return spans;
    const PlaybackClock& clock = session_->clock();
    for (const Stall& stall : run->stalls) {
        const double start = clock.simToReplay(stall.start);
        const double end = clock.simToReplay(stall.end);
        // A stall before the first frame is start-up, which the replay does not show.
        if (end > start)
            spans.append({start, end});
    }
    return spans;
}

void Scrubber::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    const double height = hovered_ || dragging_ ? kHovered : kTrack;
    const QRectF track(kKnob, (this->height() - height) / 2.0, std::max(1.0, width() - 2.0 * kKnob), height);
    QPainterPath shape;
    shape.addRoundedRect(track, height / 2.0, height / 2.0);

    QColor rest = text;
    rest.setAlphaF(0.16F);
    painter.fillPath(shape, rest);
    if (!session_->run())
        return;

    painter.save();
    painter.setClipPath(shape);
    const double now = session_->clock().simToReplay(session_->cursor()->time());
    QColor played = text;
    played.setAlphaF(0.72F);
    painter.fillRect(QRectF(track.left(), track.top(), xAt(now) - track.left(), track.height()), played);
    // The stalls, over both parts; at least two pixels, so a short one stays visible.
    for (const auto& [start, end] : stallSpans()) {
        const double left = xAt(start);
        painter.fillRect(QRectF(left, track.top(), std::max(2.0, xAt(end) - left), track.height()), stallColor());
    }
    painter.restore();

    // The knob, white with a soft edge, at the cursor.
    const QPointF centre(xAt(now), this->height() / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 60));
    painter.drawEllipse(centre + QPointF(0, 0.8), kKnob, kKnob);
    painter.setBrush(Qt::white);
    painter.drawEllipse(centre, kKnob - 0.5, kKnob - 0.5);
}

void Scrubber::seekTo(double x)
{
    if (!session_->run())
        return;
    session_->cursor()->setTime(session_->clock().replayToSim(replayAt(x)), TimeCursor::Source::Timeline);
}

void Scrubber::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    dragging_ = true;
    seekTo(event->position().x());
    update();
}

void Scrubber::mouseMoveEvent(QMouseEvent* event)
{
    const double x = event->position().x();
    if (dragging_) {
        seekTo(x);
        return;
    }
    if (!session_->run())
        return;
    // The time under the pointer, and whether the replay is frozen there.
    const double replay = replayAt(x);
    QString tip = clockText(replay);
    for (const auto& [start, end] : stallSpans()) {
        if (replay >= start && replay <= end)
            tip += tr(" · stalled %1 s").arg(end - start, 0, 'f', 1);
    }
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
}

void Scrubber::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Scrubber::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void Scrubber::leaveEvent(QEvent* event)
{
    hovered_ = false;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

} // namespace abrviz
