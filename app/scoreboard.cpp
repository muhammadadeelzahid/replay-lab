#include "scoreboard.h"

#include "algorithm_colors.h"
#include "format.h"

#include "abrviz/core/load/bundle_loader.h"

#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace abrviz {

namespace {

/// Marks a cell as the best of its column.
constexpr int kBestRole = Qt::UserRole + 1;

/// How a column's values are read, shown and ranked.
struct ColumnSpec {
    Scoreboard::Column column;
    std::function<std::optional<double>(const QoeReport&)> value;  ///< empty: not known
    std::function<QString(double)> text;
    bool higherIsBetter;
};

const std::vector<ColumnSpec>& columns()
{
    static const std::vector<ColumnSpec> specs{
        {Scoreboard::Bitrate, [](const QoeReport& r) { return std::optional(r.averagePlayedBitrate); },
         [](double v) { return format::bitrate(v); }, true},
        {Scoreboard::VmafMean, [](const QoeReport& r) { return r.meanVmafShown; },
         [](double v) { return QString::number(v, 'f', 1); }, true},
        {Scoreboard::VmafMin, [](const QoeReport& r) { return r.minVmafShown; },
         [](double v) { return QString::number(v, 'f', 1); }, true},
        {Scoreboard::BelowVmaf60, [](const QoeReport& r) { return r.timeBelowVmaf60; },
         [](double v) { return format::seconds(v, 1); }, false},
        {Scoreboard::Switches, [](const QoeReport& r) { return std::optional(static_cast<double>(r.playedSwitches)); },
         [](double v) { return QString::number(v, 'f', 0); }, false},
        {Scoreboard::Stalls, [](const QoeReport& r) { return std::optional(static_cast<double>(r.stallEpisodes)); },
         [](double v) { return QString::number(v, 'f', 0); }, false},
        {Scoreboard::StalledTime, [](const QoeReport& r) { return std::optional(r.rebufferTime); },
         [](double v) { return format::seconds(v, 2); }, false},
        {Scoreboard::Startup, [](const QoeReport& r) { return std::optional(r.startupDelay); },
         [](double v) { return format::seconds(v, 3); }, false},
    };
    return specs;
}

} // namespace

Scoreboard::Scoreboard(QWidget* parent)
    : QWidget(parent)
    , title_(new QLabel(this))
    , table_(new QTableWidget(0, ColumnCount, this))
{
    setObjectName(QStringLiteral("scoreboard"));
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    table_->setHorizontalHeaderLabels({tr("Algorithm"), tr("Bitrate"), tr("VMAF"), tr("Lowest VMAF"),
                                       tr("VMAF < 60"), tr("Switches"), tr("Stalls"),
                                       tr("Stalled"), tr("Startup")});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(Algorithm, QHeaderView::ResizeToContents);
    table_->setCursor(Qt::PointingHandCursor);
    table_->setToolTip(tr("Click an algorithm to see it side by side with the open run"));
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < static_cast<int>(entries_.size())) {
            const QString directory = entries_[static_cast<std::size_t>(row)]->bundle->directory;
            if (QDir(directory).absolutePath() != QDir(open_).absolutePath())
                emit compareRequested(directory);
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(6);
    layout->addWidget(title_);
    layout->addWidget(table_);
    hide();
}

Scoreboard::~Scoreboard()
{
    for (auto& entry : entries_) {
        if (entry->watcher)
            entry->watcher->disconnect(this);
    }
}

void Scoreboard::setExperiment(const QList<BundleSummaryPtr>& runs, const QString& open)
{
    open_ = open;
    // Keep the entries (and their reports) of runs that are still in the experiment.
    std::vector<std::unique_ptr<Entry>> kept;
    for (const BundleSummaryPtr& run : runs) {
        std::unique_ptr<Entry> entry;
        for (auto& old : entries_) {
            if (old && old->bundle->directory == run->directory)
                entry = std::move(old);
        }
        if (!entry) {
            entry = std::make_unique<Entry>();
            entry->bundle = run;
            entry->watcher = new QFutureWatcher<LoadResult>(this);
            Entry* raw = entry.get();
            connect(entry->watcher, &QFutureWatcherBase::finished, this, [this, raw] {
                const auto future = raw->watcher->future();
                std::ignore = raw->load.release();
                if (future.isCanceled() || future.resultCount() == 0 || !future.result().run)
                    return;
                raw->report = computeQoe(*future.result().run);
                fillTable();
                if (rowsFilled() == static_cast<int>(entries_.size()))
                    emit filled();
            });
            auto future = loadBundle(run->directory);
            entry->load.reset(future);
            entry->watcher->setFuture(future);
        }
        kept.push_back(std::move(entry));
    }
    for (auto& old : entries_) {
        if (old && old->watcher)
            old->watcher->disconnect(this);
    }
    entries_ = std::move(kept);
    setVisible(entries_.size() >= 2);
    if (!entries_.empty()) {
        const BundleSummary& first = *entries_.front()->bundle;
        title_->setText(tr("%1 at %2: every algorithm")
                            .arg(first.videoTitle.isEmpty() ? first.videoName : first.videoTitle,
                                 first.linkRate ? format::bitrate(static_cast<double>(*first.linkRate))
                                                : tr("an uncongested link")));
    }
    fillTable();
}

int Scoreboard::rowsFilled() const
{
    int filled = 0;
    for (const auto& entry : entries_)
        filled += entry->report ? 1 : 0;
    return filled;
}

QString Scoreboard::cell(int row, Column column) const
{
    const QTableWidgetItem* item = table_->item(row, column);
    return item ? item->text() : QString();
}

bool Scoreboard::isBest(int row, Column column) const
{
    const QTableWidgetItem* item = table_->item(row, column);
    return item && item->data(kBestRole).toBool();
}

void Scoreboard::fillTable()
{
    table_->setRowCount(static_cast<int>(entries_.size()));
    // The best value of each column among the runs loaded so far.
    std::vector<std::optional<double>> best(ColumnCount);
    for (const ColumnSpec& spec : columns()) {
        for (const auto& entry : entries_) {
            if (!entry->report)
                continue;
            const auto v = spec.value(*entry->report);
            if (!v || !std::isfinite(*v))
                continue;
            auto& b = best[static_cast<std::size_t>(spec.column)];
            if (!b || (spec.higherIsBetter ? *v > *b : *v < *b))
                b = *v;
        }
    }
    // Green for the best value (the accent is the LLM's colour).
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor good = dark ? QColor(0x3d, 0xd6, 0x8c) : QColor(0x1e, 0x9e, 0x5e);
    for (int row = 0; row < static_cast<int>(entries_.size()); ++row) {
        const Entry& entry = *entries_[static_cast<std::size_t>(row)];
        const bool open = QDir(entry.bundle->directory).absolutePath() == QDir(open_).absolutePath();
        auto* name = new QTableWidgetItem(algorithmDisplayName(entry.bundle->algorithm) + (open ? tr("  (open)") : QString()));
        name->setForeground(algorithmColor(entry.bundle->algorithm, palette()));
        QFont font = name->font();
        font.setBold(true);
        name->setFont(font);
        table_->setItem(row, Algorithm, name);
        for (const ColumnSpec& spec : columns()) {
            auto* item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            const auto v = entry.report ? spec.value(*entry.report) : std::nullopt;
            if (!entry.report) {
                item->setText(tr("…"));
            } else if (!v) {
                item->setText(QStringLiteral("–"));
            } else {
                item->setText(spec.text(*v));
                const auto& b = best[static_cast<std::size_t>(spec.column)];
                const bool isBest = b && std::abs(*v - *b) < 1e-9;
                item->setData(kBestRole, isBest);
                if (isBest) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                    item->setForeground(good);
                }
            }
            if (open) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            table_->setItem(row, spec.column, item);
        }
    }
    table_->setFixedHeight(table_->horizontalHeader()->height()
                           + table_->verticalHeader()->defaultSectionSize() * std::max(1, table_->rowCount()) + 4);
}

} // namespace abrviz
