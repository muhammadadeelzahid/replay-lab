#include "charts_panel.h"

#include "chart_series.h"
#include "format.h"
#include "icons.h"
#include "session.h"
#include "time_cursor.h"

#include <QAreaSeries>
#include <QChart>
#include <QChartView>
#include <QGraphicsLayout>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QLineSeries>
#include <QMouseEvent>
#include <QScatterSeries>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace abrviz {

namespace {

/** @brief Space left of the plot area for the y-axis labels, in pixels. */
constexpr double kLeftMargin = 52.0;
/** @brief Space right of the plot area, in pixels. */
constexpr double kRightMargin = 52.0;  // room for the loss axis
constexpr double kTopMargin = 6.0;  ///< Space above the plot area, in pixels.
/** @brief Space below the plot area of a chart without time labels, in pixels. */
constexpr double kBottomMargin = 6.0;
/** @brief Space below the plot area of the last chart, which shows the time labels, in pixels. */
constexpr double kAxisLabelsHeight = 22.0;

const QColor kPrimary{0x2f, 0x6d, 0xb5};    ///< Main series of a chart (blue).
const QColor kSecondary{0x6b, 0x6b, 0x6b};  ///< Second series of a chart (grey).
const QColor kStall{0xd6, 0x27, 0x28};      ///< Stalls (red).
const QColor kOverride{0xe6, 0x55, 0x0d};   ///< Guard overrides of the LLM's choice (orange).

/**
 * @brief The last element at or before a time.
 *
 * @tparam T Element type with a `time` member.
 * @param items Elements sorted by `time`.
 * @param t Simulation time in seconds.
 * @return The last element with `time <= t`, or null if there is none.
 */
template <typename T>
const T* lastAtOrBefore(const std::vector<T>& items, double t)
{
    const auto it = std::ranges::upper_bound(items, t, {}, &T::time);
    return it == items.begin() ? nullptr : &*std::prev(it);
}

/**
 * @brief A coloured legend mark followed by its label, as rich text.
 *
 * @param color Colour of the mark.
 * @param label Label after the mark; HTML-escaped.
 * @param hollow True for a hollow circle (unfilled points), false for a filled square.
 * @return The HTML fragment.
 */
QString swatch(const QColor& color, const QString& label, bool hollow = false)
{
    return QStringLiteral("<span style='color:%1'>%2</span>&nbsp;%3")
        .arg(color.name(), hollow ? QStringLiteral("&#9675;") : QStringLiteral("&#9632;"),
             label.toHtmlEscaped());
}

/**
 * @brief Rounds an axis maximum up to a round number.
 *
 * Returns the smallest of 1, 2, 2.5, 4, 5 or 8 times a power of ten that is at least `value`,
 * so that the axis maximum and its half (the tick labels) are round numbers.
 *
 * @param value The largest value to show.
 * @return The rounded maximum; 1 if `value` is 0 or negative.
 */
double niceCeiling(double value)
{
    if (value <= 0.0)
        return 1.0;
    const double power = std::pow(10.0, std::floor(std::log10(value)));
    for (const double factor : {1.0, 2.0, 2.5, 4.0, 5.0, 8.0, 10.0}) {
        if (factor * power >= value * (1.0 - 1e-9))
            return factor * power;
    }
    return 10.0 * power;
}

/**
 * @brief Tick interval for a time axis.
 *
 * @param range Length of the axis in seconds.
 * @return The smallest of 1, 2, 5, 10, 15, 20, 30 or 60 s that gives at most 10 intervals;
 *         60 s for longer axes.
 */
double niceStep(double range)
{
    static constexpr std::array steps = {1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0, 60.0};
    for (const double step : steps) {
        if (range / step <= 10.0)
            return step;
    }
    return steps.back();
}

} // namespace

/**
 * @brief A chart view with a fixed plot area and clicks reported as simulation times.
 *
 * The fixed plot area makes all charts share the same x mapping. Clicks and drags with the left
 * button are reported through `onTime`.
 */
class ChartView final : public QChartView {
public:
    /**
     * @brief Creates the view with antialiasing and a minimum height of 90 pixels.
     *
     * @param chart The chart to show; the view takes ownership.
     * @param parent Parent widget.
     */
    ChartView(QChart* chart, QWidget* parent)
        : QChartView(chart, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setMinimumHeight(90);
    }

    std::function<void(double)> onTime;   ///< Receives the time clicked or dragged to.
    double bottomMargin = kBottomMargin;  ///< Room below the plot area (more for axis labels).

    /**
     * @brief Sets the plot area to the chart size minus the fixed margins.
     */
    void updatePlotArea()
    {
        const QSizeF size = chart()->size();
        chart()->setPlotArea(QRectF(kLeftMargin, kTopMargin,
                                    std::max(1.0, size.width() - kLeftMargin - kRightMargin),
                                    std::max(1.0, size.height() - kTopMargin - bottomMargin)));
        // With a fixed plot area, Qt Charts moves the series but not the axes on a resize.
        chart()->layout()->invalidate();
    }

protected:
    /**
     * @brief Resizes the chart and recomputes the plot area.
     *
     * @param event The resize event.
     */
    void resizeEvent(QResizeEvent* event) override
    {
        QChartView::resizeEvent(event);
        updatePlotArea();
    }

    /**
     * @brief Reports the time under a left-button press.
     *
     * @param event The mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            report(event->position());
        QChartView::mousePressEvent(event);
    }

    /**
     * @brief Reports the time under the mouse while the left button is held.
     *
     * @param event The mouse event.
     */
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton)
            report(event->position());
        QChartView::mouseMoveEvent(event);
    }

private:
    /**
     * @brief Converts a view position to a simulation time and passes it to `onTime`.
     *
     * The position is clamped to the plot area. Does nothing without a callback or while the
     * chart has no series.
     *
     * @param pos Position in view coordinates.
     */
    void report(QPointF pos)
    {
        if (!onTime || chart()->series().isEmpty())
            return;
        const QPointF inChart = chart()->mapFromScene(mapToScene(pos.toPoint()));
        const QRectF plot = chart()->plotArea();
        const auto* axis = static_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
        const double fraction = std::clamp((inChart.x() - plot.left()) / plot.width(), 0.0, 1.0);
        onTime(axis->min() + fraction * (axis->max() - axis->min()));
    }
};

/**
 * @brief One chart of the panel: its legend, view, axes and cursor line.
 *
 * Also tracks the largest y value added on each axis, from which finishRanges() sets the axis
 * ranges.
 */
struct ChartsPanel::Plot {
    QLabel* legend = nullptr;  ///< Rich-text legend above the chart.
    ChartView* view = nullptr;  ///< The chart's view; child of the panel.
    QChart* chart = nullptr;  ///< Owned by the view.
    QValueAxis* x = nullptr;   ///< Time axis in seconds, at the bottom; owned by the chart.
    QValueAxis* y = nullptr;   ///< Left value axis; owned by the chart.
    QValueAxis* y2 = nullptr;  ///< Only for RTT and loss.
    QGraphicsLineItem* cursor = nullptr;  ///< Child item of the chart.
    double yMax = 0.0;   ///< Largest value added on the left axis.
    double y2Max = 0.0;  ///< Largest value added on the right axis.

    /**
     * @brief Adds a line series.
     *
     * @param name Series name.
     * @param points Points of the line.
     * @param pen Pen to draw the line with.
     * @param secondAxis True to plot against the right axis (`y2`).
     * @return The series, owned by the chart.
     */
    QLineSeries* addLine(const QString& name, const QList<QPointF>& points, const QPen& pen,
                         bool secondAxis = false)
    {
        auto* series = new QLineSeries();
        series->setName(name);
        series->setPen(pen);
        series->replace(points);
        attach(series, points, secondAxis);
        return series;
    }

    /**
     * @brief Adds a scatter series against the left axis.
     *
     * @param name Series name.
     * @param points The points.
     * @param color Colour of the markers.
     * @param size Marker size in pixels.
     * @param hollow True for unfilled markers.
     * @return The series, owned by the chart.
     */
    QScatterSeries* addPoints(const QString& name, const QList<QPointF>& points,
                              const QColor& color, double size, bool hollow = false)
    {
        auto* series = new QScatterSeries();
        series->setName(name);
        series->setMarkerSize(size);
        series->setColor(hollow ? QColor(Qt::transparent) : color);
        series->setBorderColor(color);
        series->setPen(QPen(color, hollow ? 1.5 : 0.5));
        series->replace(points);
        attach(series, points, false);
        return series;
    }

    /**
     * @brief Adds a series to the chart, attaches it to the axes and raises the axis maximum.
     *
     * @param series The series; the chart takes ownership.
     * @param points The series' points, used for the maximum.
     * @param secondAxis True to attach it to the right axis (`y2`).
     */
    void attach(QXYSeries* series, const QList<QPointF>& points, bool secondAxis)
    {
        chart->addSeries(series);  // takes ownership
        series->attachAxis(x);
        series->attachAxis(secondAxis ? y2 : y);
        double& max = secondAxis ? y2Max : yMax;
        for (const QPointF& p : points)
            max = std::max(max, p.y());
    }

    /**
     * @brief Removes all series and resets the axis maxima.
     */
    void clear()
    {
        chart->removeAllSeries();
        yMax = 0.0;
        y2Max = 0.0;
    }

    /**
     * @brief Sets the y-axis ranges from 0 to a round number above the largest value plus 5%.
     */
    void finishRanges()
    {
        y->setRange(0.0, niceCeiling(yMax * 1.05));
        if (y2)
            y2->setRange(0.0, niceCeiling(y2Max * 1.05));
    }
};

ChartsPanel::ChartsPanel(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , readout_(new QLabel(this))
{
    setObjectName(QStringLiteral("chartsPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    readout_->setTextFormat(Qt::RichText);
    // The readout changes with every cursor move; a fixed height keeps it from relaying out
    // the window (which costs a full repaint).
    readout_->setWordWrap(true);
    readout_->setFixedHeight(readout_->fontMetrics().lineSpacing() * 2 + 4);
    readout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(readout_);

    const QColor text = palette().color(QPalette::Text);
    QColor grid = text;
    grid.setAlphaF(0.12F);

    for (int i = 0; i < kChartCount; ++i) {
        auto plot = std::make_unique<Plot>();
        plot->chart = new QChart();
        plot->chart->legend()->hide();
        plot->chart->setBackgroundRoundness(0);
        plot->chart->setBackgroundBrush(palette().base());
        plot->chart->setMargins({0, 0, 0, 0});
        plot->chart->layout()->setContentsMargins(0, 0, 0, 0);
        plot->view = new ChartView(plot->chart, this);  // the view owns the chart

        auto makeAxis = [&](Qt::Alignment alignment) {
            auto* axis = new QValueAxis();
            axis->setLabelsBrush(text);
            axis->setGridLineColor(grid);
            axis->setLinePenColor(grid);
            QFont font = axis->labelsFont();
            font = format::scaled(font, 0.85);
            axis->setLabelsFont(font);
            plot->chart->addAxis(axis, alignment);  // takes ownership
            return axis;
        };
        plot->x = makeAxis(Qt::AlignBottom);
        plot->x->setTickType(QValueAxis::TicksDynamic);
        plot->x->setTickAnchor(0.0);
        plot->x->setLabelFormat(QStringLiteral("%.0f s"));
        plot->y = makeAxis(Qt::AlignLeft);
        plot->y->setTickCount(3);
        plot->y->setLabelFormat(QStringLiteral("%g"));
        if (static_cast<Chart>(i) == Chart::RttLoss) {
            plot->y2 = makeAxis(Qt::AlignRight);
            plot->y2->setTickCount(3);
            plot->y2->setLabelFormat(QStringLiteral("%g"));
        }
        const bool last = i == kChartCount - 1;
        plot->x->setLabelsVisible(last);
        plot->view->bottomMargin = last ? kAxisLabelsHeight : kBottomMargin;

        plot->cursor = new QGraphicsLineItem(plot->chart);
        plot->cursor->setPen(QPen(accentColor(palette()), 2));
        plot->cursor->setZValue(100);
        plot->cursor->hide();

        plot->legend = new QLabel(this);
        plot->legend->setTextFormat(Qt::RichText);

        plot->view->onTime = [this](double time) {
            if (run_)
                session_->cursor()->setTime(time, TimeCursor::Source::Charts);
        };
        connect(plot->chart, &QChart::plotAreaChanged, this, &ChartsPanel::updateCursor);

        layout->addWidget(plot->legend);
        layout->addWidget(plot->view, last ? 5 : 4);  // the last one also holds the axis labels
        plots_[static_cast<std::size_t>(i)] = std::move(plot);
    }

    connect(session_, &Session::runChanged, this, &ChartsPanel::rebuild);
    connect(session_->cursor(), &TimeCursor::timeChanged, this, [this] {
        updateCursor();
        updateReadout();
    });
    rebuild();
}

ChartsPanel::~ChartsPanel() = default;

QChart* ChartsPanel::chart(Chart chart) const
{
    return plots_[static_cast<std::size_t>(chart)]->chart;
}

int ChartsPanel::seriesCount(Chart which, const QString& name) const
{
    const auto series = chart(which)->series();
    return static_cast<int>(std::ranges::count_if(
        series, [&](const QAbstractSeries* s) { return s->name() == name; }));
}

double ChartsPanel::xAt(Chart which, double time) const
{
    const Plot& plot = *plots_[static_cast<std::size_t>(which)];
    const QRectF area = plot.chart->plotArea();
    const double span = plot.x->max() - plot.x->min();
    if (span <= 0.0)
        return area.left();
    return area.left() + (time - plot.x->min()) / span * area.width();
}

double ChartsPanel::cursorX(Chart which) const
{
    const Plot& plot = *plots_[static_cast<std::size_t>(which)];
    if (!plot.cursor->isVisible())
        return std::numeric_limits<double>::quiet_NaN();
    return plot.cursor->line().x1();
}

void ChartsPanel::rebuild()
{
    run_ = session_->run();
    const double end = std::max(1.0, session_->endTime());
    for (auto& plot : plots_) {
        plot->clear();
        plot->x->setRange(0.0, end);
        plot->x->setTickInterval(niceStep(end));
    }
    if (run_) {
        fillBuffer(*plots_[0], *run_);
        fillThroughput(*plots_[1], *run_);
        fillBitrate(*plots_[2], *run_);
        fillRttLoss(*plots_[3], *run_);
        fillDecisionDelay(*plots_[4], *run_);
    }
    for (auto& plot : plots_)
        plot->finishRanges();
    updateCursor();
    updateReadout();
}

void ChartsPanel::fillBuffer(Plot& plot, const RunData& run)
{
    QList<QPointF> points;
    points.reserve(static_cast<qsizetype>(run.buffer.size()));
    for (const BufferPoint& p : run.buffer)
        points.append(QPointF(p.time, p.level));
    plot.addLine(QStringLiteral("buffer"), points, QPen(kPrimary, 1.5));

    // Stalls as shaded bands from 0 to the top of the buffer range.
    const double top = std::max(1.0, plot.yMax * 1.1);
    for (const Stall& stall : run.stalls) {
        auto* upper = new QLineSeries();
        upper->append(stall.start, top);
        upper->append(std::max(stall.end, stall.start + 0.2), top);  // visible when short
        auto* area = new QAreaSeries(upper);  // takes ownership of `upper`
        area->setName(QStringLiteral("stall"));
        QColor fill = kStall;
        fill.setAlphaF(0.25F);
        area->setBrush(fill);
        area->setPen(Qt::NoPen);
        plot.chart->addSeries(area);
        area->attachAxis(plot.x);
        area->attachAxis(plot.y);
    }
    plot.legend->setText(tr("<b>Buffer</b> (s) &nbsp; %1 &nbsp; %2")
                             .arg(swatch(kPrimary, tr("level")), swatch(kStall, tr("stall"))));
}

void ChartsPanel::fillThroughput(Plot& plot, const RunData& run)
{
    // Measured: the rate of each download while it ran.
    std::vector<StepInterval> downloads;
    for (const Segment& segment : run.segments) {
        const double duration = segment.doneTime - segment.requestTime;
        if (duration > 0.0) {
            downloads.push_back({segment.requestTime, segment.doneTime,
                                 static_cast<double>(segment.bytes) * 8.0 / duration / 1e6});
        }
    }
    for (const auto& points : stepRuns(downloads, 0.01))
        plot.addLine(QStringLiteral("measured"), points, QPen(kPrimary, 1.5));

    // The policy's estimate at each decision.
    for (const auto& points : contiguousRuns(run.samples, &Sample::time, &Sample::throughput, 1e-6)) {
        auto* line = plot.addLine(QStringLiteral("estimate"), points,
                                  QPen(kSecondary, 1.5, Qt::DashLine));
        line->setPointsVisible(true);
    }

    // Some downloads are logged as finishing a few milliseconds after their request, which
    // gives rates far above the link (and, in one run, an estimate built from them). The axis
    // stops at the link rate, and anything faster runs off the top. Without a link limit the
    // highest estimate sets the scale.
    double limit = 0.0;
    if (run.info.feederLinkRate) {
        limit = static_cast<double>(*run.info.feederLinkRate) / 1e6;
    } else {
        for (const Sample& sample : run.samples)
            limit = std::max(limit, sample.throughput.value_or(0.0) / 1e6);
    }
    const bool clipped = limit > 0.0 && plot.yMax > limit * 1.1;
    if (limit > 0.0)
        plot.yMax = std::min(plot.yMax, limit * 1.1);

    QString legend = tr("<b>Throughput</b> (Mbps) &nbsp; %1 &nbsp; %2")
                         .arg(swatch(kPrimary, tr("download")),
                              swatch(kSecondary, tr("policy estimate")));
    if (clipped)
        legend += tr(" &nbsp; (values above the link rate are cut off at the top)");
    plot.legend->setText(legend);
}

void ChartsPanel::fillBitrate(Plot& plot, const RunData& run)
{
    const VideoInfo& video = run.video;
    auto mbps = [&](int rung) {
        const Rung* r = video.rung(rung);
        return r ? static_cast<double>(r->bitrate) / 1e6 : 0.0;
    };

    std::vector<StepInterval> shown;
    const double frame = run.info.playerSegmentDuration / kFramesPerSegment;
    for (const SegmentPlayback& p : run.playback) {
        const auto it = std::ranges::find(run.segments, p.segment, &Segment::index);
        if (it == run.segments.end())
            continue;
        // A stall can fall inside a segment; the line stops for it.
        double start = p.firstFrameTime;
        const double end = p.lastFrameTime + frame;
        for (const Stall& stall : run.stalls) {
            if (stall.start > start && stall.start < end) {
                shown.push_back({start, stall.start, mbps(it->playedRung)});
                start = std::max(start, stall.end);
            }
        }
        if (end > start)
            shown.push_back({start, end, mbps(it->playedRung)});
    }
    for (const auto& points : stepRuns(shown, 2.0 * frame))
        plot.addLine(QStringLiteral("played"), points, QPen(kPrimary, 2.0));

    QList<QPointF> chosen;
    QList<QPointF> overridden;
    QList<QPointF> wanted;
    for (const Decision& d : run.decisions) {
        if (!d.used)
            continue;
        (d.guardOverride ? overridden : chosen).append(QPointF(d.time, mbps(d.chosenRung)));
        if (d.guardOverride && d.llmRung)
            wanted.append(QPointF(d.time, mbps(*d.llmRung)));
    }
    plot.addPoints(QStringLiteral("chosen"), chosen, kSecondary, 6.0);
    plot.addPoints(QStringLiteral("override"), overridden, kOverride, 8.0);
    plot.addPoints(QStringLiteral("llm"), wanted, kOverride, 8.0, true);
    // The whole ladder fits, so rung changes are comparable between runs.
    if (!video.ladder.empty())
        plot.yMax = std::max(plot.yMax, mbps(video.ladder.back().index));
    plot.legend->setText(tr("<b>Bitrate</b> (Mbps) &nbsp; %1 &nbsp; %2 &nbsp; %3 &nbsp; %4")
                             .arg(swatch(kPrimary, tr("on screen")),
                                  swatch(kSecondary, tr("chosen")),
                                  swatch(kOverride, tr("guard override")),
                                  swatch(kOverride, tr("LLM's choice"), true)));
}

void ChartsPanel::fillRttLoss(Plot& plot, const RunData& run)
{
    for (const auto& points : contiguousRuns(run.samples, &Sample::time, &Sample::rttMs)) {
        auto* line = plot.addLine(QStringLiteral("rtt"), points, QPen(kPrimary, 1.5));
        line->setPointsVisible(true);
    }
    for (const auto& points : contiguousRuns(run.samples, &Sample::time, &Sample::lossRate)) {
        auto* line = plot.addLine(QStringLiteral("loss"), points,
                                  QPen(kSecondary, 1.5, Qt::DashLine), true);
        line->setPointsVisible(true);
    }
    plot.legend->setText(tr("<b>RTT</b> (ms, left) &nbsp; %1 &nbsp; %2")
                             .arg(swatch(kPrimary, tr("RTT")),
                                  swatch(kSecondary, tr("loss, as recorded (right)"))));
}

void ChartsPanel::fillDecisionDelay(Plot& plot, const RunData& run)
{
    std::vector<Decision> used;
    std::ranges::copy_if(run.decisions, std::back_inserter(used), &Decision::used);
    for (const auto& points : contiguousRuns(used, &Decision::time, &Decision::decisionDelay)) {
        auto* line = plot.addLine(QStringLiteral("delay"), points, QPen(kPrimary, 1.0));
        line->setPointsVisible(true);
    }
    plot.legend->setText(tr("<b>Decision delay</b> (s) &nbsp; %1")
                             .arg(swatch(kPrimary, tr("time the policy took"))));
}

void ChartsPanel::updateCursor()
{
    const double time = session_->cursor()->time();
    for (std::size_t i = 0; i < plots_.size(); ++i) {
        Plot& plot = *plots_[i];
        if (!run_) {
            plot.cursor->hide();
            continue;
        }
        const QRectF area = plot.chart->plotArea();
        const double x = xAt(static_cast<Chart>(i), time);
        plot.cursor->setLine(x, area.top(), x, area.bottom());
        plot.cursor->show();
    }
}

void ChartsPanel::updateReadout()
{
    if (!run_) {
        readout_->setText(tr("No run open."));
        return;
    }
    const RunData& run = *run_;
    const double t = session_->cursor()->time();
    const QString none = QStringLiteral("-");
    QStringList parts;
    parts << tr("<b>%1</b>").arg(format::seconds(t, 2));

    const BufferPoint* buffer = lastAtOrBefore(run.buffer, t);
    parts << tr("buffer %1").arg(buffer && t <= run.playbackEnd ? format::seconds(buffer->level)
                                                                : none);

    const auto stall = std::ranges::find_if(
        run.stalls, [t](const Stall& s) { return s.start <= t && t < s.end; });
    if (stall != run.stalls.end()) {
        parts << tr("<span style='color:%1'>stalled</span>").arg(kStall.name());
    } else if (const SegmentPlayback* shown = run.playbackAt(t)) {
        const auto it = std::ranges::find(run.segments, shown->segment, &Segment::index);
        parts << tr("segment %1 at %2")
                     .arg(shown->segment)
                     .arg(it != run.segments.end() ? format::rungLabel(run.video, it->playedRung)
                                                   : none);
    }

    const Sample* sample = lastAtOrBefore(run.samples, t);
    auto value = [&](const std::optional<double>& v, double scale, int decimals,
                     const QString& unit) {
        return v ? QStringLiteral("%1 %2").arg(*v * scale, 0, 'f', decimals).arg(unit) : none;
    };
    parts << tr("estimate %1").arg(sample ? value(sample->throughput, 1e-6, 1, tr("Mbps")) : none);
    parts << tr("RTT %1").arg(sample ? value(sample->rttMs, 1.0, 0, tr("ms")) : none);
    parts << tr("loss %1").arg(sample ? value(sample->lossRate, 1.0, 2, QString()).trimmed()
                                      : none);
    readout_->setText(parts.join(QStringLiteral(" &nbsp;·&nbsp; ")));
}

QString ChartsPanel::readout() const
{
    return readout_->text();
}

} // namespace abrviz
