#include "run_pane.h"

#include "algorithm_colors.h"
#include "format.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace abrviz {

RunPane::RunPane(Session* session, ReplayPlayer* player, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
    , name_(new QLabel(this))
    , choice_(new QComboBox(this))
    , status_(new QLabel(this))
    , stall_(new QLabel(tr("stalled"), this))
    , header_(new QWidget(this))
{
    setObjectName(QStringLiteral("runPane"));
    QFont bold = name_->font();
    bold.setBold(true);
    name_->setFont(bold);
    status_->setForegroundRole(QPalette::PlaceholderText);
    status_->setTextFormat(Qt::RichText);
    stall_->setStyleSheet(QStringLiteral(
        "color: white; background: #ff453a; border-radius: 8px; padding: 1px 8px; font-weight: 600;"));
    stall_->hide();
    choice_->hide();
    choice_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(choice_, &QComboBox::activated, this, [this](int index) {
        emit runChosen(choice_->itemData(index).toString());
    });

    for (QWidget* w : std::initializer_list<QWidget*>{name_, choice_, stall_, status_})
        w->setParent(header_);
    auto* header = new QHBoxLayout(header_);
    header->setContentsMargins(10, 6, 10, 6);
    header->setSpacing(10);
    header->addWidget(name_);
    header->addWidget(choice_);
    header->addWidget(stall_);
    header->addStretch(1);
    header->addWidget(status_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    player_->setParent(this);
    layout->addWidget(player_, 1);
    player_->show();

    connect(session_, &Session::runChanged, this, &RunPane::refresh);
    connect(session_->cursor(), &TimeCursor::timeChanged, this, &RunPane::refresh);
    refresh();
}

void RunPane::setChoices(const QList<BundleSummaryPtr>& runs, const QString& current)
{
    const QSignalBlocker block(choice_);
    choice_->clear();
    for (const BundleSummaryPtr& run : runs)
        choice_->addItem(algorithmDisplayName(run->algorithm), run->directory);
    choice_->setCurrentIndex(std::max(0, choice_->findData(current)));
    choice_->setVisible(runs.size() > 1);
    refresh();
}

ReplayPlayer* RunPane::takePlayer()
{
    auto* player = player_;
    layout()->removeWidget(player);
    player->setParent(nullptr);
    player_ = nullptr;
    return player;
}

void RunPane::setHeaderVisible(bool visible)
{
    header_->setVisible(visible);
}

QString RunPane::headerText() const
{
    QStringList parts{name_->text()};
    if (stall_->isVisible())
        parts << stall_->text();
    parts << status_->text();
    return parts.join(QStringLiteral(" | "));
}

void RunPane::refresh()
{
    const auto& run = session_->run();
    if (!run) {
        name_->setText(tr("No run"));
        status_->clear();
        stall_->hide();
        return;
    }
    const QString key = run->info.policyName;
    const QColor color = algorithmColor(key, palette());
    name_->setText(algorithmDisplayName(key));
    name_->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    name_->setVisible(choice_->isHidden());

    // What is on screen at the cursor: quality, VMAF and buffer.
    const double time = session_->cursor()->time();
    const PlaybackClock::Position position = session_->clock().positionAtSim(time);
    stall_->setVisible(position.stalled);
    QStringList parts;
    if (position.beforeStart) {
        parts << tr("starting");
    } else if (position.segment >= 0 && position.segment < static_cast<int>(run->segments.size())) {
        const Segment& segment = run->segments[static_cast<std::size_t>(position.segment)];
        if (const Rung* rung = run->video.rung(segment.playedRung))
            parts << QStringLiteral("%1p · %2").arg(rung->height).arg(format::bitrate(static_cast<double>(rung->bitrate)));
        if (segment.quality.vmaf)
            parts << tr("VMAF %1").arg(*segment.quality.vmaf, 0, 'f', 0);
    }
    // The buffer level last reported at or before the cursor.
    const auto points = run->bufferBetween(run->streamStart, time);
    if (!points.empty())
        parts << tr("buffer %1 s").arg(points.back().level, 0, 'f', 1);
    status_->setText(parts.join(QStringLiteral(" &nbsp;·&nbsp; ")));
}

} // namespace abrviz
