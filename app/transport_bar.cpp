#include "transport_bar.h"

#include "format.h"
#include "replay_player.h"
#include "segment_timeline.h"
#include "session.h"
#include "time_cursor.h"

#include <QAction>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace abrviz {

namespace {

QString stateName(ReplaySource::State state)
{
    switch (state) {
    case ReplaySource::State::Idle: return TransportBar::tr("Idle");
    case ReplaySource::State::Preparing: return TransportBar::tr("Preparing");
    case ReplaySource::State::Playing: return TransportBar::tr("Playing");
    case ReplaySource::State::Paused: return TransportBar::tr("Paused");
    case ReplaySource::State::Stalled: return TransportBar::tr("Stalled");
    case ReplaySource::State::Ended: return TransportBar::tr("Ended");
    case ReplaySource::State::Error: return TransportBar::tr("Error");
    }
    return {};
}

QColor stateColor(ReplaySource::State state)
{
    switch (state) {
    case ReplaySource::State::Playing: return {0x2e, 0x9e, 0x5b};
    case ReplaySource::State::Stalled: return {0xd0, 0x45, 0x3a};
    case ReplaySource::State::Error: return {0xd0, 0x45, 0x3a};
    case ReplaySource::State::Preparing: return {0xd8, 0x8c, 0x1a};
    default: return {0x7a, 0x80, 0x8c};
    }
}

} // namespace

TransportBar::TransportBar(Session* session, ReplayPlayer* player, SegmentTimeline* timeline,
                           const Actions& actions, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
    , time_(new QLabel(this))
    , state_(new QLabel(this))
{
    setObjectName(QStringLiteral("transportBar"));

    auto button = [this](QAction* action, int size) {
        auto* b = new QToolButton(this);
        b->setDefaultAction(action);
        b->setAutoRaise(true);
        b->setIconSize(QSize(size, size));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return b;
    };
    auto* controls = new QHBoxLayout();
    controls->setSpacing(2);
    controls->addWidget(button(actions.previousSegment, 18));
    controls->addWidget(button(actions.stepBack, 18));
    QToolButton* play = button(actions.play, 26);
    play->setObjectName(QStringLiteral("playButton"));
    controls->addWidget(play);
    controls->addWidget(button(actions.stepForward, 18));
    controls->addWidget(button(actions.nextSegment, 18));
    controls->addSpacing(12);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    time_->setFont(mono);
    time_->setFixedWidth(QFontMetrics(mono).horizontalAdvance(QString(46, QLatin1Char('0'))));
    controls->addWidget(time_);
    controls->addStretch(1);
    QFont bold = state_->font();
    bold.setBold(true);
    state_->setFont(bold);
    state_->setAlignment(Qt::AlignCenter);
    state_->setFixedWidth(QFontMetrics(bold).horizontalAdvance(QStringLiteral("Preparing")) + 24);
    controls->addWidget(state_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 6);
    layout->setSpacing(2);
    layout->addLayout(controls);
    layout->addWidget(timeline);

    connect(session_->cursor(), &TimeCursor::timeChanged, this, &TransportBar::update);
    connect(session_, &Session::runChanged, this, &TransportBar::update);
    connect(player_, &ReplayPlayer::stateChanged, this, &TransportBar::update);
    connect(player_, &ReplayPlayer::engineChanged, this, &TransportBar::update);
    update();
}

QString TransportBar::timeText() const
{
    return time_->text();
}

QString TransportBar::stateText() const
{
    return state_->text();
}

void TransportBar::update()
{
    if (!session_->run()) {
        time_->clear();
        state_->clear();
        return;
    }
    const double sim = session_->cursor()->time();
    const PlaybackClock& clock = session_->clock();
    time_->setText(tr("sim %1   replay %2 / %3")
                       .arg(format::seconds(sim).rightJustified(9),
                            format::seconds(clock.simToReplay(sim)).rightJustified(9),
                            format::seconds(clock.replayDuration())));
    const ReplaySource* engine = player_->engine();
    const ReplaySource::State state = engine ? engine->state() : ReplaySource::State::Idle;
    state_->setText(stateName(state));
    const QColor color = stateColor(state);
    state_->setStyleSheet(QStringLiteral("QLabel { color: white; background: %1; border-radius: 9px; padding: 2px 8px; }")
                              .arg(color.name()));
}

} // namespace abrviz
