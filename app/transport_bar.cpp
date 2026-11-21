#include "transport_bar.h"

#include "format.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

/**
 * @brief Replay time as QuickTime shows it, with tenths: 1:05.3.
 *
 * @param seconds The time in seconds; negative values are shown as 0:00.0.
 * @return The time as minutes, then seconds with one decimal.
 */
QString clockText(double seconds)
{
    // Rounded to tenths first, so 59.96 s shows as 1:00.0 rather than 0:60.0.
    seconds = std::round(std::max(0.0, seconds) * 10.0) / 10.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, 4, 'f', 1, QLatin1Char('0'));
}

/**
 * @brief The translated name of a replay engine state.
 *
 * @param state The state.
 * @return Its name, e.g. "Playing"; empty for an unknown value.
 */
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

} // namespace

TransportBar::TransportBar(Session* session, ReplayPlayer* player, QWidget* scrubber,
                           const Actions& actions, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
    , time_(new QLabel(this))
    , remaining_(new QLabel(this))
    , state_(new QLabel(this))
{
    setObjectName(QStringLiteral("transportBar"));
    setAttribute(Qt::WA_StyledBackground);

    auto button = [this](QAction* action, int size) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("transportButton"));
        b->setDefaultAction(action);
        b->setIconSize(QSize(size, size));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // Digits that do not move as they change.
    QFont digits = font();
    digits.setFeature(QFont::Tag("tnum"), 1);
    digits = format::scaled(digits, 1.05);
    for (QLabel* label : {time_, remaining_}) {
        label->setFont(digits);
        label->setFixedWidth(QFontMetrics(digits).horizontalAdvance(QStringLiteral("-00:00.0")) + 8);
    }
    time_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    remaining_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    remaining_->setForegroundRole(QPalette::PlaceholderText);
    state_->setForegroundRole(QPalette::PlaceholderText);
    state_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    state_->setFixedWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("Preparing · sim 000.00 s")) + 8);

    // The buttons centred, the player's state on the right; under them the bar, between the
    // time played and the time left, as in Apple's players.
    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    QToolButton* watch = actions.watch ? button(actions.watch, 18) : nullptr;
    // Balances the state (and the full-screen button) on the right, so the buttons are centred.
    controls->addSpacing(state_->width() + (watch ? watch->sizeHint().width() + controls->spacing() : 0));
    controls->addStretch(1);
    controls->addWidget(button(actions.previousSegment, 16));
    controls->addWidget(button(actions.stepBack, 18));
    QToolButton* play = button(actions.play, 26);
    play->setObjectName(QStringLiteral("playButton"));
    controls->addWidget(play);
    controls->addWidget(button(actions.stepForward, 18));
    controls->addWidget(button(actions.nextSegment, 16));
    controls->addStretch(1);
    controls->addWidget(state_);
    if (watch)
        controls->addWidget(watch);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(10);
    bar->addWidget(time_);
    bar->addWidget(scrubber, 1);
    bar->addWidget(remaining_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 4, 14, 10);
    layout->setSpacing(2);
    layout->addLayout(controls);
    layout->addLayout(bar);

    setStyleSheet(QStringLiteral(R"(
        QToolButton#transportButton, QToolButton#playButton {
            border: none; background: transparent; padding: 4px; border-radius: 6px;
        }
        QToolButton#transportButton:hover, QToolButton#playButton:hover {
            background: #2e808080;
        }
        QToolButton#transportButton:pressed, QToolButton#playButton:pressed {
            background: #52808080;
        }
    )"));

    connect(session_->cursor(), &TimeCursor::timeChanged, this, &TransportBar::update);
    connect(session_, &Session::runChanged, this, &TransportBar::update);
    connect(player_, &ReplayPlayer::stateChanged, this, &TransportBar::update);
    connect(player_, &ReplayPlayer::engineChanged, this, &TransportBar::update);
    update();
}

QString TransportBar::timeText() const
{
    return time_->text() + QLatin1Char(' ') + remaining_->text();
}

QString TransportBar::stateText() const
{
    return state_->text();
}

void TransportBar::update()
{
    if (!session_->run()) {
        time_->clear();
        remaining_->clear();
        state_->clear();
        return;
    }
    const double sim = session_->cursor()->time();
    const PlaybackClock& clock = session_->clock();
    const double replay = clock.simToReplay(sim);
    time_->setText(clockText(replay));
    remaining_->setText(QStringLiteral("-") + clockText(clock.replayDuration() - replay));
    const ReplaySource* engine = player_->engine();
    const ReplaySource::State state = engine ? engine->state() : ReplaySource::State::Idle;
    const QString text = tr("%1 · sim %2").arg(stateName(state), format::seconds(sim));
    if (state == ReplaySource::State::Stalled || state == ReplaySource::State::Error)
        state_->setText(QStringLiteral("<span style='color:#ff453a'>%1</span>").arg(text.toHtmlEscaped()));
    else
        state_->setText(text);
}

} // namespace abrviz
