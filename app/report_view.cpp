#include "report_view.h"

#include "format.h"
#include "session.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace abrviz {

namespace {

/**
 * @brief Columns of the report table; ColumnCount is the number of columns.
 */
enum Column { Metric, Value, Exported, Check, ColumnCount };

/**
 * @brief Formats the value of a summary check by its kind.
 *
 * Bitrates and throughputs are shown as bitrates, delays and times in seconds (three
 * decimals), everything else as an integer count.
 *
 * @param name Name of the check, which tells its kind.
 * @param value The value (bits per second, seconds or a count).
 * @return The formatted value.
 */
QString formatCheck(const QString& name, double value)
{
    if (name.contains(QLatin1String("bitrate")) || name.contains(QLatin1String("throughput")))
        return format::bitrate(value);
    if (name.contains(QLatin1String("delay")) || name.contains(QLatin1String("time")))
        return format::seconds(value, 3);
    return QString::number(value, 'f', 0);
}

} // namespace

ReportView::ReportView(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , verdict_(new QLabel(this))
    , table_(new QTableWidget(0, ColumnCount, this))
{
    setObjectName(QStringLiteral("reportView"));
    verdict_->setWordWrap(true);
    verdict_->setContentsMargins(6, 4, 6, 4);
    table_->setHorizontalHeaderLabels({tr("Metric"), tr("Value"), tr("summary.json"), QString()});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->horizontalHeader()->setSectionResizeMode(Metric, QHeaderView::Stretch);
    for (const int column : {Value, Exported, Check})
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(verdict_);
    layout->addWidget(table_, 1);

    connect(session_, &Session::runChanged, this, &ReportView::refresh);
    refresh();
}

QString ReportView::verdict() const
{
    return verdict_->text();
}

QStringList ReportView::rows() const
{
    QStringList rows;
    for (int r = 0; r < table_->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < ColumnCount; ++c) {
            const QTableWidgetItem* item = table_->item(r, c);
            cells << (item ? item->text() : QString());
        }
        rows << cells.join(QStringLiteral(" | "));
    }
    return rows;
}

void ReportView::addSection(const QString& title)
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* item = new QTableWidgetItem(title);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    table_->setItem(row, Metric, item);
    table_->setSpan(row, Metric, 1, ColumnCount);
}

void ReportView::addRow(const QString& metric, const QString& value, const QString& exported,
                        std::optional<bool> agrees)
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, Metric, new QTableWidgetItem(QStringLiteral("  ") + metric));
    auto* valueItem = new QTableWidgetItem(value);
    valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, Value, valueItem);
    auto* exportedItem = new QTableWidgetItem(exported);
    exportedItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, Exported, exportedItem);
    if (agrees) {
        auto* check = new QTableWidgetItem(*agrees ? tr("agrees") : tr("differs"));
        check->setForeground(*agrees ? QColor(0x2e, 0x9e, 0x5b) : QColor(0xd0, 0x45, 0x3a));
        table_->setItem(row, Check, check);
    }
}

void ReportView::refresh()
{
    table_->setRowCount(0);
    const auto& run = session_->run();
    if (!run) {
        report_ = {};
        verdict_->setText(tr("No run open."));
        return;
    }
    report_ = computeQoe(*run);
    const QoeReport& r = report_;
    const PlaybackClock& clock = session_->clock();

    addSection(tr("What the viewer saw"));
    QString shown = QString::number(r.segmentsShown) + tr(" of %1").arg(r.segmentsTotal);
    if (r.cutOff)
        shown += tr(" (cut off in segment %1 after %2 frames)").arg(r.lastSegmentShown).arg(r.lastSegmentFrames);
    addRow(tr("Segments shown"), shown);
    addRow(tr("Average bitrate played"), format::bitrate(r.averagePlayedBitrate));
    addRow(tr("Quality switches played"), QString::number(r.playedSwitches));
    addRow(tr("Replay length"), format::seconds(clock.replayDuration()));
    addRow(tr("Simulated playback"), format::seconds(clock.simEnd() - clock.simStart()));

    addSection(tr("Policy"));
    addRow(tr("Decisions"), QString::number(r.decisions));
    addRow(tr("Guard overrides"), QString::number(r.guardOverrides));
    addRow(tr("Decision delay, average"), format::seconds(r.averageDecisionDelay, 4));
    addRow(tr("Decision delay, maximum"), format::seconds(r.maxDecisionDelay, 4));

    addSection(tr("Checked against summary.json"));
    int disagreeing = 0;
    int compared = 0;
    for (const SummaryCheck& check : r.checks) {
        const bool present = check.exported.has_value();
        const bool agrees = present && check.agrees();
        compared += present ? 1 : 0;
        disagreeing += present && !agrees ? 1 : 0;
        QString name = check.name;
        name[0] = name[0].toUpper();
        addRow(name, formatCheck(check.name, check.computed),
               present ? formatCheck(check.name, *check.exported) : tr("not reported"),
               present ? std::optional<bool>(agrees) : std::nullopt);
    }
    if (disagreeing == 0)
        verdict_->setText(tr("All %1 numbers that summary.json reports agree with the values "
                             "computed from the bundle (within 1 %).").arg(compared));
    else
        verdict_->setText(tr("%1 of %2 numbers differ from summary.json.").arg(disagreeing).arg(compared));
}

} // namespace abrviz
