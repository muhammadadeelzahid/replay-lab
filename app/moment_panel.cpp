#include "moment_panel.h"

#include "format.h"
#include "replay_player.h"
#include "rung_colors.h"
#include "session.h"
#include "time_cursor.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace abrviz {

namespace {

constexpr int kWidth = 280;

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.05);
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
    guard_ = addRow(tr("Guard"));

    addHeading(tr("Network"));
    estimate_ = addRow(tr("Estimate"));
    rtt_ = addRow(tr("RTT"));

    addHeading(tr("Replay"));
    engine_ = addRow(tr("Engine"));
    simTime_ = addRow(tr("Simulation"));
    replayTime_ = addRow(tr("Replay"));

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
        guard_->setText(decision->guardOverride
                            ? QStringLiteral("<b style='color:#e07020'>%1</b>")
                                  .arg(tr("overrode LLM (rung %1)").arg(decision->llmRung.value_or(-1)))
                            : tr("kept the LLM's choice"));
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
    if (rtt)
        rtt_->setText(QStringLiteral("%1 ms").arg(*rtt, 0, 'f', 0));

    const ReplaySource* engine = player_->engine();
    engine_->setText(engine ? engine->name() : tr("none"));
    simTime_->setText(format::seconds(time));
    replayTime_->setText(tr("%1 of %2").arg(format::seconds(clock.simToReplay(time)),
                                           format::seconds(clock.replayDuration())));
}

} // namespace abrviz
