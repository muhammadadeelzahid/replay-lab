#include "segment_timeline.h"

#include "format.h"
#include "rung_colors.h"
#include "session.h"
#include "time_cursor.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace abrviz {

namespace {

constexpr double kLabelWidth = 84.0;
constexpr double kRightMargin = 12.0;
constexpr double kTopMargin = 6.0;
constexpr double kLaneHeight = 26.0;
constexpr double kLaneGap = 6.0;
constexpr double kAxisHeight = 20.0;
constexpr double kPointWidth = 7.0;
constexpr double kMinStallWidth = 4.0;  // a short stall stays visible at any zoom

constexpr std::array kLanes = {SegmentTimeline::Lane::Downloads, SegmentTimeline::Lane::Playback,
                               SegmentTimeline::Lane::Decisions};

const QColor kStallColor(0xd6, 0x27, 0x28);
const QColor kOverrideColor(0xe6, 0x55, 0x0d);

bool isPoint(SegmentTimeline::Kind kind)
{
    using K = SegmentTimeline::Kind;
    return kind == K::CutOff || kind == K::Decision || kind == K::GuardOverride;
}

QString laneName(SegmentTimeline::Lane lane)
{
    switch (lane) {
    case SegmentTimeline::Lane::Downloads: return SegmentTimeline::tr("Downloads");
    case SegmentTimeline::Lane::Playback: return SegmentTimeline::tr("On screen");
    case SegmentTimeline::Lane::Decisions: return SegmentTimeline::tr("Decisions");
    }
    return {};
}

QString span(double start, double end)
{
    return QStringLiteral("%1 to %2 s").arg(start, 0, 'f', 2).arg(end, 0, 'f', 2);
}

/// A tick step that leaves at least `minPixels` between labels.
double tickStep(double secondsPerPixel, double minPixels)
{
    static constexpr std::array steps = {0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0, 60.0, 120.0};
    for (const double step : steps) {
        if (step / secondsPerPixel >= minPixels)
            return step;
    }
    return steps.back();
}

} // namespace

SegmentTimeline::SegmentTimeline(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setObjectName(QStringLiteral("segmentTimeline"));
    setMouseTracking(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(session_, &Session::runChanged, this, &SegmentTimeline::rebuild);
    // Repaint for every cursor move, including the ones this widget caused: it draws the line.
    connect(session_->cursor(), &TimeCursor::timeChanged, this, qOverload<>(&QWidget::update));
    rebuild();
}

int SegmentTimeline::count(Kind kind) const
{
    return static_cast<int>(std::ranges::count(items_, kind, &Item::kind));
}

void SegmentTimeline::rebuild()
{
    run_ = session_->run();
    endTime_ = session_->endTime();
    items_.clear();
    if (!run_) {
        update();
        return;
    }
    const RunData& run = *run_;
    const VideoInfo& video = run.video;

    std::unordered_map<int, const Segment*> segmentByIndex;
    for (const Segment& segment : run.segments)
        segmentByIndex.emplace(segment.index, &segment);

    for (const Segment& segment : run.segments) {
        QString text = tr("Segment %1 downloaded %2\n%3, %4 MB")
                           .arg(segment.index)
                           .arg(span(segment.requestTime, segment.doneTime),
                                format::rungLabel(video, segment.playedRung))
                           .arg(static_cast<double>(segment.bytes) / 1e6, 0, 'f', 2);
        if (!segment.played)
            text += tr("\nNever shown by the simulated player");
        items_.push_back({Lane::Downloads, Kind::Download, segment.requestTime, segment.doneTime,
                          segment.playedRung, !segment.played, text});
    }

    for (const Decision& decision : run.decisions) {
        if (decision.bufferFullWait && *decision.bufferFullWait > 0.0) {
            const double end = decision.time + *decision.bufferFullWait;
            items_.push_back({Lane::Downloads, Kind::BufferFullWait, decision.time, end, -1, false,
                              tr("Buffer full: waited %1 before requesting segment %2")
                                  .arg(format::seconds(*decision.bufferFullWait))
                                  .arg(decision.forSegment)});
        }
    }

    const double frameDuration = run.info.playerSegmentDuration / kFramesPerSegment;
    for (const SegmentPlayback& shown : run.playback) {
        const auto it = segmentByIndex.find(shown.segment);
        const int rung = it != segmentByIndex.end() ? it->second->playedRung : -1;
        QString text = tr("Segment %1 on screen %2\n%3")
                           .arg(shown.segment)
                           .arg(span(shown.firstFrameTime, shown.lastFrameTime + frameDuration),
                                format::rungLabel(video, rung));
        if (shown.framesPlayed < kFramesPerSegment)
            text += tr("\n%1 of %2 frames shown").arg(shown.framesPlayed).arg(kFramesPerSegment);
        items_.push_back({Lane::Playback, Kind::Segment, shown.firstFrameTime,
                          shown.lastFrameTime + frameDuration, rung, false, text});
    }

    for (const Stall& stall : run.stalls) {
        QString text = tr("Stall %1 (%2)").arg(span(stall.start, stall.end),
                                               format::seconds(stall.duration()));
        if (stall.terminal)
            text += tr("\nPlayback never resumed");
        items_.push_back({Lane::Playback, Kind::Stall, stall.start, stall.end, -1, false, text});
    }

    if (run.playbackCutOff() && !run.playback.empty()) {
        const SegmentPlayback& last = run.playback.back();
        const double at = last.lastFrameTime + frameDuration;
        items_.push_back({Lane::Playback, Kind::CutOff, at, at, -1, false,
                          tr("The simulation stopped at %1 in segment %2 (frame %3 of %4); "
                             "segments %5 to %6 were never shown")
                              .arg(format::seconds(run.info.simStopTime))
                              .arg(last.segment)
                              .arg(last.framesPlayed)
                              .arg(kFramesPerSegment)
                              .arg(last.segment + 1)
                              .arg(video.segmentCount - 1)});
    }

    for (const Decision& decision : run.decisions) {
        if (!decision.used)
            continue;
        if (decision.guardOverride) {
            QString text = tr("Decision for segment %1 at %2\nBuffer guard override: %3")
                               .arg(decision.forSegment)
                               .arg(format::seconds(decision.time),
                                    format::rungLabel(video, decision.chosenRung));
            if (decision.llmRung)
                text += tr(" (the LLM chose %1)").arg(format::rungLabel(video, *decision.llmRung));
            items_.push_back({Lane::Decisions, Kind::GuardOverride, decision.time, decision.time,
                              decision.chosenRung, false, text});
        } else {
            items_.push_back({Lane::Decisions, Kind::Decision, decision.time, decision.time,
                              decision.chosenRung, false,
                              tr("Decision for segment %1 at %2\n%3")
                                  .arg(decision.forSegment)
                                  .arg(format::seconds(decision.time),
                                       format::rungLabel(video, decision.chosenRung))});
        }
    }
    update();
}

QRectF SegmentTimeline::plotRect() const
{
    const double height = kLanes.size() * (kLaneHeight + kLaneGap);
    return {kLabelWidth, kTopMargin, std::max(1.0, width() - kLabelWidth - kRightMargin), height};
}

QRectF SegmentTimeline::laneRect(Lane lane) const
{
    const QRectF plot = plotRect();
    const double top = plot.top() + static_cast<int>(lane) * (kLaneHeight + kLaneGap);
    return {plot.left(), top, plot.width(), kLaneHeight};
}

double SegmentTimeline::xAt(double time) const
{
    const QRectF plot = plotRect();
    if (endTime_ <= 0.0)
        return plot.left();
    return plot.left() + time / endTime_ * plot.width();
}

double SegmentTimeline::timeAt(double x) const
{
    const QRectF plot = plotRect();
    return std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0) * endTime_;
}

QRectF SegmentTimeline::itemRect(const Item& item) const
{
    const QRectF lane = laneRect(item.lane);
    if (isPoint(item.kind)) {
        const double x = xAt(item.start);
        return {x - kPointWidth / 2, lane.top(), kPointWidth, lane.height()};
    }
    if (item.kind == Kind::BufferFullWait) {
        // The lower third of the download lane, under the bars it separates.
        const double left = xAt(item.start);
        return {left, lane.bottom() - lane.height() / 3, std::max(1.0, xAt(item.end) - left),
                lane.height() / 3};
    }
    const double left = xAt(item.start);
    const double width = xAt(item.end) - left;
    if (item.kind == Kind::Stall && width < kMinStallWidth) {
        const double centre = left + width / 2;
        return {centre - kMinStallWidth / 2, lane.top(), kMinStallWidth, lane.height()};
    }
    return {left, lane.top(), std::max(1.0, width), lane.height()};
}

const SegmentTimeline::Item* SegmentTimeline::itemAt(QPointF pos) const
{
    // Later items are drawn on top, so search from the end.
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        if (itemRect(*it).adjusted(-1, 0, 1, 0).contains(pos))
            return &*it;
    }
    return nullptr;
}

QSize SegmentTimeline::sizeHint() const
{
    return {800, minimumSizeHint().height()};
}

QSize SegmentTimeline::minimumSizeHint() const
{
    const double height = kTopMargin + kLanes.size() * (kLaneHeight + kLaneGap) + kAxisHeight;
    return {static_cast<int>(kLabelWidth) + 200, static_cast<int>(std::ceil(height))};
}

bool SegmentTimeline::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const Item* item = itemAt(help->pos()))
            QToolTip::showText(help->globalPos(), item->text, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void SegmentTimeline::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QColor text = palette().color(QPalette::Text);
    QColor faint = text;
    faint.setAlphaF(0.12F);

    // Lane labels and backgrounds.
    for (const Lane lane : kLanes) {
        const QRectF r = laneRect(lane);
        QColor tint = text;
        tint.setAlphaF(0.06F);
        painter.fillRect(r, tint);
        painter.setPen(text);
        painter.drawText(QRectF(4, r.top(), kLabelWidth - 8, r.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, laneName(lane));
    }

    if (!run_ || endTime_ <= 0.0) {
        painter.setPen(text);
        painter.drawText(plotRect(), Qt::AlignCenter, tr("No run open"));
        return;
    }

    // Time axis.
    const QRectF plot = plotRect();
    const double axisTop = plot.bottom();
    const double step = tickStep(endTime_ / plot.width(), 56.0);
    for (double t = 0.0; t <= endTime_ + 1e-9; t += step) {
        const double x = xAt(t);
        painter.setPen(faint);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, axisTop));
        painter.setPen(text);
        painter.drawText(QRectF(x - 30, axisTop, 60, kAxisHeight), Qt::AlignCenter,
                         QStringLiteral("%1 s").arg(t, 0, 'f', step < 1.0 ? 1 : 0));
    }

    const int rungCount = static_cast<int>(run_->video.ladder.size());
    QColor waitColor = text;
    waitColor.setAlphaF(0.45F);

    for (const Item& item : items_) {
        const QRectF r = itemRect(item);
        switch (item.kind) {
        case Kind::Download:
        case Kind::Segment: {
            QColor fill = item.rung >= 0 ? rungColor(item.rung, rungCount) : QColor(Qt::gray);
            if (item.dimmed)
                fill.setAlphaF(0.3F);
            painter.fillRect(r, fill);
            painter.setPen(palette().color(QPalette::Base));
            painter.drawLine(r.topLeft(), r.bottomLeft());  // separates neighbouring segments
            break;
        }
        case Kind::BufferFullWait:
            painter.fillRect(r, QBrush(waitColor, Qt::BDiagPattern));
            break;
        case Kind::Stall: {
            QColor fill = kStallColor;
            fill.setAlphaF(0.85F);
            painter.fillRect(r, fill);
            painter.fillRect(r, QBrush(Qt::white, Qt::BDiagPattern));
            break;
        }
        case Kind::CutOff:
            painter.setPen(QPen(kStallColor, 2));
            painter.drawLine(QPointF(r.center().x(), r.top() - 2), QPointF(r.center().x(), r.bottom() + 2));
            break;
        case Kind::Decision:
            painter.setPen(QPen(rungColor(item.rung, rungCount), 2));
            painter.drawLine(QPointF(r.center().x(), r.top() + r.height() / 3),
                             QPointF(r.center().x(), r.bottom()));
            break;
        case Kind::GuardOverride: {
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath triangle;
            triangle.moveTo(r.left() - 1, r.top() + 2);
            triangle.lineTo(r.right() + 1, r.top() + 2);
            triangle.lineTo(r.center().x(), r.top() + r.height() / 2);
            triangle.closeSubpath();
            painter.fillPath(triangle, kOverrideColor);
            painter.setPen(QPen(kOverrideColor, 2));
            painter.drawLine(QPointF(r.center().x(), r.top() + r.height() / 2),
                             QPointF(r.center().x(), r.bottom()));
            painter.setRenderHint(QPainter::Antialiasing, false);
            break;
        }
        }
    }

    // Cursor.
    const double x = xAt(session_->cursor()->time());
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.drawLine(QPointF(x, plot.top() - kTopMargin), QPointF(x, axisTop));
}

void SegmentTimeline::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        moveCursorTo(event->position().x());
    QWidget::mousePressEvent(event);
}

void SegmentTimeline::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
        moveCursorTo(event->position().x());
    QWidget::mouseMoveEvent(event);
}

void SegmentTimeline::moveCursorTo(double x)
{
    if (run_)
        session_->cursor()->setTime(timeAt(x), TimeCursor::Source::Timeline);
}

} // namespace abrviz
