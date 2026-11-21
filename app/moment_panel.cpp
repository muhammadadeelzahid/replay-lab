#include "moment_panel.h"

#include "format.h"
#include "replay_player.h"
#include "rung_colors.h"
#include "session.h"
#include "time_cursor.h"

#include <QGridLayout>
#include <QLabel>
#include <QTextDocumentFragment>
#include <QVBoxLayout>

#include <algorithm>

namespace abrviz {

namespace {

constexpr int kWidth = 280;  ///< Fixed width of the panel, in pixels.

/**
 * @brief A bold section heading, slightly larger than the body text.
 *
 * @param text The heading text.
 * @param parent The label's parent.
 * @return The new label, owned by `parent`.
 */
QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font = format::scaled(font, 1.05);
    label->setFont(font);
    label->setContentsMargins(0, 10, 0, 2);
    return label;
}

} // namespace

MomentPanel::MomentPanel(Session* session, ReplayPlayer* player, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
{
    setObjectName(QStringLiteral("momentPanel"));
    setFixedWidth(kWidth);

    // One grid for all sections, so the values line up.
    grid_ = new QGridLayout();
    grid_->setHorizontalSpacing(12);
    grid_->setVerticalSpacing(5);
    grid_->setColumnStretch(1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 8, 14, 10);
    layout->addLayout(grid_);
    layout->addStretch(1);

    addHeading(tr("On screen"));
    segment_ = addRow(tr("Segment"));
    quality_ = addRow(tr("Quality"));
    bitrate_ = addRow(tr("Bitrate"));
    buffer_ = addRow(tr("Buffer"));
    status_ = addRow(tr("Player"));

    addHeading(tr("Last decision"));
    decisionTime_ = addRow(tr("At"));
    chosen_ = addRow(tr("Chosen"));

    addHeading(tr("Satellite path"));
    feeder_ = addRow(tr("Feeder"));
    satellite_ = addRow(tr("Satellite"));
    carrier_ = addRow(tr("Carrier"));
    relay_ = addRow(tr("Relay"));
    estimate_ = addRow(tr("Estimate"));
    rtt_ = addRow(tr("RTT"));

    addHeading(tr("Replay"));
    engine_ = addRow(tr("Engine"));
    simTime_ = addRow(tr("Simulation"));
    replayTime_ = addRow(tr("Replay"));

    // What the colours of the timeline and the rung strips mean, for the open run's ladder.
    guideHeading_ = heading(tr("Colours"), this);
    grid_->addWidget(guideHeading_, grid_->rowCount(), 0, 1, 2);
    guide_ = new QLabel(this);
    guide_->setObjectName(QStringLiteral("colourGuide"));
    guide_->setTextFormat(Qt::RichText);
    guide_->setFont(format::scaled(guide_->font(), 0.92));
    grid_->addWidget(guide_, grid_->rowCount(), 0, 1, 2);
    connect(session_, &Session::runChanged, this, &MomentPanel::updateGuide);
    updateGuide();

    connect(session_->cursor(), &TimeCursor::timeChanged, this, &MomentPanel::refresh);
    connect(session_, &Session::runChanged, this, &MomentPanel::refresh);
    connect(player_, &ReplayPlayer::stateChanged, this, &MomentPanel::refresh);
    connect(player_, &ReplayPlayer::engineChanged, this, &MomentPanel::refresh);
    refresh();
}

void MomentPanel::addHeading(const QString& title)
{
    grid_->addWidget(heading(title, this), grid_->rowCount(), 0, 1, 2);
}

QLabel* MomentPanel::addRow(const QString& name)
{
    auto* label = new QLabel(this);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Fixed width: long values are cut, never widen the panel.
    label->setMinimumWidth(1);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* title = new QLabel(name, this);
    title->setForegroundRole(QPalette::PlaceholderText);
    const int row = grid_->rowCount();
    grid_->addWidget(title, row, 0);
    grid_->addWidget(label, row, 1);
    fields_.append({name, label});
    return label;
}

void MomentPanel::updateGuide()
{
    const auto& run = session_->run();
    guideHeading_->setVisible(run != nullptr);
    guide_->setVisible(run != nullptr);
    if (!run)
        return;
    // Two columns, so the guide stays short enough for a laptop screen.
    const auto cell = [](const QString& mark, const QString& label) {
        return QStringLiteral("<td style='padding-right:5px'>%1</td><td style='padding-right:14px'>%2</td>")
            .arg(mark, label.toHtmlEscaped());
    };
    const auto swatch = [&cell](const QColor& color, const QString& label) {
        return cell(QStringLiteral("<span style='color:%1'>&#9632;</span>").arg(color.name()), label);
    };
    QStringList cells;
    const int count = static_cast<int>(run->video.ladder.size());
    // Highest quality first, as the brightest colour.
    for (auto it = run->video.ladder.rbegin(); it != run->video.ladder.rend(); ++it) {
        cells << swatch(rungColor(it->index, count),
                        tr("%1p · %2").arg(it->height).arg(format::bitrate(static_cast<double>(it->bitrate))));
    }
    // Rungs fill the first column top to bottom, then the second.
    const int half = (static_cast<int>(cells.size()) + 1) / 2;
    QString rows;
    for (int r = 0; r < half; ++r) {
        rows += QStringLiteral("<tr>") + cells[r];
        if (r + half < cells.size())
            rows += cells[r + half];
        rows += QStringLiteral("</tr>");
    }
    QStringList marks{swatch(stallColor(), tr("Stall")), cell(QStringLiteral("&#9474;"), tr("Position"))};
    if (std::ranges::any_of(run->decisions, [](const Decision& d) { return d.guardOverride; }))
        marks.insert(1, swatch(overrideColor(), tr("Guard override")));
    for (int i = 0; i < marks.size(); i += 2)
        rows += QStringLiteral("<tr>") + marks[i] + (i + 1 < marks.size() ? marks[i + 1] : QString()) + QStringLiteral("</tr>");
    guide_->setText(QStringLiteral("<table cellspacing='0' cellpadding='1'>%1</table>").arg(rows));
}

QString MomentPanel::guideText() const
{
    return !guide_->isHidden() ? QTextDocumentFragment::fromHtml(guide_->text()).toPlainText() : QString();
}

QString MomentPanel::value(const QString& field) const
{
    for (const auto& [name, label] : fields_) {
        if (name == field)
            return label->text();
    }
    return {};
}

void MomentPanel::refresh()
{
    const auto& run = session_->run();
    for (const auto& field : fields_)
        field.second->setText(QStringLiteral("–"));
    if (!run)
        return;
    const double time = session_->cursor()->time();
    const PlaybackClock& clock = session_->clock();
    const PlaybackClock::Position position = clock.positionAtSim(time);

    if (position.beforeStart) {
        segment_->setText(tr("startup (%1)").arg(format::seconds(run->playbackStart - run->streamStart)));
    } else {
        segment_->setText(tr("%1 of %2").arg(position.segment).arg(run->video.segmentCount));
        const auto it = std::ranges::find(run->segments, position.segment, &Segment::index);
        if (it != run->segments.end()) {
            if (const Rung* rung = run->video.rung(it->playedRung)) {
                const QColor color = rungColor(rung->index, static_cast<int>(run->video.ladder.size()));
                quality_->setText(QStringLiteral("<span style='color:%1'>&#9632;</span> rung %2, %3 x %4")
                                      .arg(color.name())
                                      .arg(rung->index)
                                      .arg(rung->width)
                                      .arg(rung->height));
                bitrate_->setText(format::bitrate(static_cast<double>(rung->bitrate)));
            }
        }
    }
    const auto bufferIt = std::ranges::upper_bound(run->buffer, time, {}, &BufferPoint::time);
    if (bufferIt != run->buffer.begin() && !position.afterEnd)
        buffer_->setText(format::seconds(std::prev(bufferIt)->level));
    if (position.stalled)
        status_->setText(QStringLiteral("<b style='color:#d0453a'>%1</b>").arg(tr("stalled")));
    else if (position.beforeStart)
        status_->setText(tr("starting"));
    else if (position.afterEnd)
        status_->setText(run->playbackCutOff() ? tr("cut off by the end of the simulation") : tr("finished"));
    else
        status_->setText(tr("playing"));

    // The last decision made at or before the cursor.
    const Decision* decision = nullptr;
    for (const Decision& d : run->decisions) {
        if (d.time > time)
            break;
        decision = &d;
    }
    if (decision) {
        decisionTime_->setText(tr("%1, for segment %2").arg(format::seconds(decision->time)).arg(decision->forSegment));
        chosen_->setText(format::rungLabel(run->video, decision->chosenRung));
    }

    // The network as last measured: the newest sample at or before the cursor that has it.
    std::optional<double> estimate;
    std::optional<double> rtt;
    for (const Sample& sample : run->samples) {
        if (sample.time > time)
            break;
        if (sample.throughput)
            estimate = sample.throughput;
        if (sample.rttMs)
            rtt = sample.rttMs;
    }
    if (decision && decision->throughputEstimate)
        estimate = decision->throughputEstimate;
    if (estimate)
        estimate_->setText(format::bitrate(*estimate));
    const NetworkTopology& net = run->topology;
    if (run->info.feederLinkRate)
        feeder_->setText(tr("%1, shared by %2 UEs")
                             .arg(format::bitrate(static_cast<double>(*run->info.feederLinkRate)))
                             .arg(run->info.simulatedUes));
    if (net.satelliteAltitude)
        satellite_->setText(tr("LEO gNB at %1 km").arg(*net.satelliteAltitude / 1000.0, 0, 'f', 0));
    if (net.carrierFrequencyGHz) {
        QString carrier = tr("%1 GHz").arg(*net.carrierFrequencyGHz, 0, 'g', 3);
        if (net.bandwidthMHz)
            carrier += tr(", %1 MHz").arg(*net.bandwidthMHz, 0, 'f', 0);
        carrier_->setText(carrier);
    }
    if (net.viaRelay && net.accessDistance)
        relay_->setText(tr("IAB node, %1 m from the UE").arg(*net.accessDistance, 0, 'f', 0));
    else if (net.relayCount > 0)
        relay_->setText(tr("IAB (%n relay(s))", nullptr, net.relayCount));
    if (rtt)
        rtt_->setText(QStringLiteral("%1 ms").arg(*rtt, 0, 'f', 0));

    const ReplaySource* engine = player_->engine();
    engine_->setText(engine ? engine->name() : tr("none"));
    simTime_->setText(format::seconds(time));
    replayTime_->setText(tr("%1 of %2").arg(format::seconds(clock.simToReplay(time)),
                                           format::seconds(clock.replayDuration())));
}

} // namespace abrviz
